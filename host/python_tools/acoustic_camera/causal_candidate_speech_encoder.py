"""Small causal speech/tail/nonspeech encoder for candidate-steered beams.

This module deliberately does not localize a source.  A spatial front-end
proposes a direction, a delay-and-sum beam is formed for that direction, and
this encoder answers only whether the beam currently contains active speech,
late speech reverberation, or nonspeech.  Direction ranking remains a separate
task.

The implementation is intentionally compact and deployable: deterministic
32-bin log-Mel features, two depthwise-separable causal convolutions, one GRU,
and a three-class linear head.  No future frame is read.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np
import torch
from torch import nn
from torch.nn import functional as F

from .speaker_voiceprint import mel_filterbank


CAUSAL_CANDIDATE_SPEECH_ENCODER_VERSION = "mic16_candidate_speech_encoder_v2_erratum1"
CLASS_NAMES = ("SPEECH", "TAIL", "NONSPEECH")


@dataclass(frozen=True)
class LogMelConfig:
    sample_rate_hz: int = 16000
    mel_bins: int = 32
    frame_length: int = 400
    hop_length: int = 160
    fft_size: int = 512
    preemphasis: float = 0.97
    floor: float = 1e-10

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def causal_log_mel(signal: np.ndarray, config: LogMelConfig = LogMelConfig()) -> np.ndarray:
    """Return [time,mel] features using only samples at or before each frame end."""

    values = np.asarray(signal, dtype=np.float32).reshape(-1)
    if values.size < int(config.frame_length):
        raise ValueError("candidate beam is shorter than one log-Mel frame")
    if not np.isfinite(values).all():
        raise ValueError("candidate beam must be finite")
    # 整段语音均值会泄露当前帧结束后的样本。
    # 以下因果预加重去除平稳直流，不访问未来。
    emphasized = np.empty_like(values)
    emphasized[0] = values[0]
    emphasized[1:] = values[1:] - float(config.preemphasis) * values[:-1]
    count = 1 + (values.size - int(config.frame_length)) // int(config.hop_length)
    offsets = np.arange(int(config.frame_length))[None, :] + int(config.hop_length) * np.arange(count)[:, None]
    frames = emphasized[offsets]
    frames = frames * np.hanning(int(config.frame_length))[None, :]
    power = np.square(np.abs(np.fft.rfft(frames, n=int(config.fft_size), axis=1))) / float(config.fft_size)
    filters = mel_filterbank(int(config.sample_rate_hz), int(config.fft_size), int(config.mel_bins))
    features = np.log(np.maximum(power @ filters.T, float(config.floor)))
    return np.asarray(features, dtype=np.float32)


class CausalDepthwiseBlock(nn.Module):
    def __init__(self, input_channels: int, output_channels: int, kernel_size: int = 5) -> None:
        super().__init__()
        self.left_context = int(kernel_size) - 1
        self.depthwise = nn.Conv1d(
            int(input_channels), int(input_channels), int(kernel_size), groups=int(input_channels)
        )
        self.pointwise = nn.Conv1d(int(input_channels), int(output_channels), 1)
        self.activation = nn.SiLU()

    def forward(self, values: torch.Tensor) -> torch.Tensor:
        values = F.pad(values, (self.left_context, 0))
        return self.activation(self.pointwise(self.depthwise(values)))

    def step(self, current: torch.Tensor, history: torch.Tensor) -> torch.Tensor:
        """Compute one output from current input and the previous K-1 inputs."""

        if current.ndim != 2 or history.ndim != 3:
            raise ValueError("current must be [batch,channel] and history [batch,K-1,channel]")
        if history.shape[:2] != (current.shape[0], self.left_context) or history.shape[2] != current.shape[1]:
            raise ValueError("causal convolution history has the wrong shape")
        window = torch.cat((history, current.unsqueeze(1)), dim=1).transpose(1, 2)
        return self.activation(self.pointwise(self.depthwise(window))).squeeze(-1)


class CausalCandidateSpeechEncoder(nn.Module):
    """Causal 32-bin log-Mel classifier with an explicit recurrent state."""

    def __init__(self, mel_bins: int = 32, hidden_channels: int = 64, class_count: int = 3) -> None:
        super().__init__()
        self.mel_bins = int(mel_bins)
        self.hidden_channels = int(hidden_channels)
        self.class_count = int(class_count)
        self.block1 = CausalDepthwiseBlock(self.mel_bins, self.hidden_channels)
        self.block2 = CausalDepthwiseBlock(self.hidden_channels, self.hidden_channels)
        self.gru = nn.GRU(self.hidden_channels, self.hidden_channels, batch_first=True)
        self.classifier = nn.Linear(self.hidden_channels, self.class_count)

    def forward(
        self,
        log_mel: torch.Tensor,
        state: torch.Tensor | None = None,
    ) -> tuple[torch.Tensor, torch.Tensor]:
        if log_mel.ndim != 3 or int(log_mel.shape[-1]) != self.mel_bins:
            raise ValueError(f"log_mel must have shape [batch,time,{self.mel_bins}]")
        values = log_mel.transpose(1, 2)
        values = self.block2(self.block1(values)).transpose(1, 2)
        values, next_state = self.gru(values, state)
        return self.classifier(values), next_state

    def parameter_count(self) -> int:
        return int(sum(parameter.numel() for parameter in self.parameters()))

    def initial_stream_state(self, batch_size: int, *, device: torch.device | None = None,
                             dtype: torch.dtype = torch.float32) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor]:
        """Return the two causal-convolution histories and GRU state."""

        batch = int(batch_size)
        if batch <= 0:
            raise ValueError("batch_size must be positive")
        raw = torch.zeros(batch, self.block1.left_context, self.mel_bins, device=device, dtype=dtype)
        hidden = torch.zeros(batch, self.block2.left_context, self.hidden_channels, device=device, dtype=dtype)
        gru = torch.zeros(1, batch, self.hidden_channels, device=device, dtype=dtype)
        return raw, hidden, gru

    def stream_step(
        self,
        current_log_mel: torch.Tensor,
        state: tuple[torch.Tensor, torch.Tensor, torch.Tensor],
    ) -> tuple[torch.Tensor, tuple[torch.Tensor, torch.Tensor, torch.Tensor]]:
        """Score one 10-ms Mel frame; the returned state belongs to one beam."""

        if current_log_mel.ndim != 2 or current_log_mel.shape[1] != self.mel_bins:
            raise ValueError(f"current_log_mel must be [batch,{self.mel_bins}]")
        raw_history, hidden_history, gru_state = state
        first = self.block1.step(current_log_mel, raw_history)
        second = self.block2.step(first, hidden_history)
        recurrent, next_gru = self.gru(second.unsqueeze(1), gru_state)
        next_state = (
            torch.cat((raw_history[:, 1:], current_log_mel.unsqueeze(1)), dim=1),
            torch.cat((hidden_history[:, 1:], first.unsqueeze(1)), dim=1),
            next_gru,
        )
        return self.classifier(recurrent[:, 0]), next_state


def speech_probability(logits: torch.Tensor) -> torch.Tensor:
    """Return P(SPEECH); TAIL and NONSPEECH never authorize a new direction."""

    if logits.shape[-1] not in (2, len(CLASS_NAMES)):
        raise ValueError("logit class dimension must be binary or the registered three classes")
    return torch.softmax(logits, dim=-1)[..., 0]


def select_speech_candidate(
    rank_scores: np.ndarray,
    candidate_speech_probabilities: np.ndarray,
    *,
    threshold: float = 0.5,
    budget: int = 4,
) -> int | None:
    """Choose the highest R2-ranked candidate that the speech head accepts."""

    ranks = np.asarray(rank_scores, dtype=np.float64).reshape(-1)
    probabilities = np.asarray(candidate_speech_probabilities, dtype=np.float64).reshape(-1)
    if ranks.shape != probabilities.shape or not np.isfinite(ranks).all():
        raise ValueError("rank and speech arrays must be finite and equal length")
    if np.any(~np.isfinite(probabilities)) or np.any((probabilities < 0) | (probabilities > 1)):
        raise ValueError("speech probabilities must be finite and within [0,1]")
    if not 0 < float(threshold) < 1 or int(budget) <= 0:
        raise ValueError("invalid acceptance threshold or candidate budget")
    # 分数相同则稳定保留上游候选顺序。
    for index in np.argsort(-ranks, kind="stable")[: int(budget)]:
        if probabilities[index] >= float(threshold):
            return int(index)
    return None
