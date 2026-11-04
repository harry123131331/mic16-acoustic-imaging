from __future__ import annotations

import math

import numpy as np

from mic16_qt.core.models import AcousticFrame, CandidateEstimate, SourceEstimate


class SyntheticAcousticSource:
    """用于界面和集成测试的确定性无硬件数据源，全程明确标注合成。覆盖显示数据路径，但不是板端结果或算法精度实验。"""

    def __init__(self, sample_rate_hz: int = 16_000, chunk_samples: int = 512) -> None:
        self.sample_rate_hz = int(sample_rate_hz)
        self.chunk_samples = int(chunk_samples)
        self.frame_index = 0
        self._sample_index = 0
        self._rng = np.random.default_rng(16_2026)
        self.azimuth_axis_deg = np.linspace(-60.0, 60.0, 121, dtype=np.float32)
        self.elevation_axis_deg = np.linspace(-30.0, 30.0, 61, dtype=np.float32)
        self._mic_angles = np.linspace(0.0, 2.0 * np.pi, 16, endpoint=False)

    def next_frame(self) -> AcousticFrame:
        n = np.arange(self.chunk_samples, dtype=np.float64) + self._sample_index
        time_s = n / self.sample_rate_hz
        phase = self.frame_index * 0.045
        source_specs = (
            (-18.0 + 2.0 * math.sin(phase * 0.35), -17.0, 720.0, 0.58),
            (14.0 + 3.0 * math.sin(phase * 0.23), 18.0, 1180.0, 0.36),
        )

        pcm = np.zeros((self.chunk_samples, 16), dtype=np.float64)
        for azimuth_deg, elevation_deg, frequency_hz, amplitude in source_specs:
            az = math.radians(azimuth_deg)
            el_scale = math.cos(math.radians(elevation_deg))
            spatial_phase = 0.72 * el_scale * np.cos(az - self._mic_angles)
            pcm += amplitude * np.sin(
                2.0 * np.pi * frequency_hz * time_s[:, None] + spatial_phase[None, :]
            )
        pcm += 0.045 * self._rng.standard_normal(pcm.shape)
        pcm = np.clip(pcm, -1.0, 1.0).astype(np.float32)

        enhanced = (
            0.82 * np.sin(2.0 * np.pi * source_specs[0][2] * time_s)
            + 0.025 * self._rng.standard_normal(self.chunk_samples)
        ).astype(np.float32)
        enhanced = np.clip(enhanced, -1.0, 1.0)

        window = np.hanning(self.chunk_samples).astype(np.float32)
        frequency_hz = np.fft.rfftfreq(self.chunk_samples, 1.0 / self.sample_rate_hz)
        raw_spectrum_db = self._spectrum_db(pcm[:, 0] * window)
        enhanced_spectrum_db = self._spectrum_db(enhanced * window)

        response_db = self._response_map(source_specs)
        sources = tuple(
            SourceEstimate(
                source_id=f"S{index + 1}",
                azimuth_deg=float(spec[0] + (0.7 if index == 0 else -0.8)),
                elevation_deg=float(spec[1] + (0.5 if index == 0 else -0.6)),
                evidence_db=float(-1.2 - index * 3.8),
            )
            for index, spec in enumerate(source_specs)
        )
        candidates = sources + (
            SourceEstimate("C3", 43.0, -6.0, -14.8, "REJECT"),
        )
        candidate_payload = tuple(
            CandidateEstimate(
                azimuth_deg=item.azimuth_deg,
                elevation_deg=item.elevation_deg,
                evidence_db=item.evidence_db,
                accepted=item.state == "TRACK",
            )
            for item in candidates
        )

        input_rms = self._rms_dbfs(pcm[:, 0])
        output_rms = self._rms_dbfs(enhanced)
        frame = AcousticFrame(
            frame_index=self.frame_index,
            sample_rate_hz=self.sample_rate_hz,
            pcm=pcm,
            enhanced=enhanced,
            frequency_hz=frequency_hz.astype(np.float32),
            raw_spectrum_db=raw_spectrum_db,
            enhanced_spectrum_db=enhanced_spectrum_db,
            azimuth_axis_deg=self.azimuth_axis_deg,
            elevation_axis_deg=self.elevation_axis_deg,
            response_db=response_db,
            sources=sources,
            candidates=candidate_payload,
            packet_loss=0,
            latency_ms=21.4 + 0.7 * math.sin(phase),
            input_rms_dbfs=input_rms,
            output_rms_dbfs=output_rms,
            snr_gain_db=7.6 + 0.3 * math.sin(phase * 0.5),
            algorithm_name="Frozen v1 / TF-zone + WDAS",
            evidence_source="SYNTHETIC",
        )
        frame.validate()
        self.frame_index += 1
        self._sample_index += self.chunk_samples
        return frame

    def _response_map(self, source_specs: tuple[tuple[float, float, float, float], ...]) -> np.ndarray:
        az_grid, el_grid = np.meshgrid(self.azimuth_axis_deg, self.elevation_axis_deg)
        linear = np.full_like(az_grid, 10.0 ** (-30.0 / 10.0), dtype=np.float64)
        for azimuth_deg, elevation_deg, _frequency_hz, amplitude in source_specs:
            az_width = 8.5 if amplitude > 0.5 else 7.0
            el_width = 7.5 if amplitude > 0.5 else 6.5
            gaussian = np.exp(
                -0.5 * ((az_grid - azimuth_deg) / az_width) ** 2
                -0.5 * ((el_grid - elevation_deg) / el_width) ** 2
            )
            linear += (amplitude**2) * gaussian
        linear += 0.015 * np.exp(-0.5 * ((az_grid - 42.0) / 11.0) ** 2 - 0.5 * ((el_grid + 6.0) / 8.0) ** 2)
        response_db = 10.0 * np.log10(np.maximum(linear, 1e-12))
        response_db -= float(np.max(response_db))
        return np.maximum(response_db, -30.0).astype(np.float32)

    @staticmethod
    def _spectrum_db(signal: np.ndarray) -> np.ndarray:
        magnitude = np.abs(np.fft.rfft(signal))
        value = 20.0 * np.log10(np.maximum(magnitude, 1e-8))
        value -= np.max(value)
        return np.maximum(value, -80.0).astype(np.float32)

    @staticmethod
    def _rms_dbfs(signal: np.ndarray) -> float:
        rms = float(np.sqrt(np.mean(np.square(signal.astype(np.float64)))))
        return 20.0 * math.log10(max(rms, 1e-9))

