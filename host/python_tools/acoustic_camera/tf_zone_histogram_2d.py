"""Time-frequency dominant-zone voting for anonymous MIC16 Az/El sources.

Speech sources need not dominate the complete broadband frame at the same
time.  This PC research frontend lets each narrow time-frequency zone vote
for one direction, then forms anonymous direction candidates from persistent
clusters of votes.  It adapts the single-source-zone/histogram principle to
the existing front-hemisphere MIC16 grid.

The module exposes raw evidence and explicit thresholds.  It is not speaker
identification, does not know the source count, and is not an FPGA block.
"""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from mic16_channel_map import apply_channel_map, default_channel_map
from mic16_config import Mic16Config, compact32_pairs

from .geometry_2d import AzElGrid
from .geometry_2d import direction_unit_vector
from .orthogonal_phat_candidates_2d import build_phat_signatures
from .valid_source_detection import angular_distance_deg


TF_ZONE_HISTOGRAM_VERSION = "mic16_tf_zone_histogram_azel_dev1"


@dataclass(frozen=True)
class TfZoneAnalysisConfig:
    frequency_min_hz: float = 300.0
    frequency_max_hz: float = 8000.0
    zone_width_hz: float = 320.0
    spatial_second_peak_nms_deg: float = 15.0

    def __post_init__(self) -> None:
        if self.frequency_min_hz < 0.0 or self.frequency_max_hz <= self.frequency_min_hz:
            raise ValueError("frequency range must be increasing")
        if self.zone_width_hz <= 0.0:
            raise ValueError("zone_width_hz must be positive")
        if self.spatial_second_peak_nms_deg <= 0.0:
            raise ValueError("spatial_second_peak_nms_deg must be positive")


@dataclass(frozen=True)
class TfZoneThresholds:
    minimum_energy_excess_db: float = 3.0
    minimum_spatial_coherence: float = 0.15
    minimum_peak_contrast: float = 0.02
    fusion_radius_deg: float = 10.0
    output_nms_radius_deg: float = 15.0
    minimum_vote_count: int = 8
    minimum_frame_support: int = 4
    minimum_coarse_band_support: int = 2
    max_candidates: int = 8

    def __post_init__(self) -> None:
        if not -20.0 <= float(self.minimum_energy_excess_db) <= 40.0:
            raise ValueError("minimum_energy_excess_db out of range")
        if not 0.0 <= float(self.minimum_spatial_coherence) <= 1.0:
            raise ValueError("minimum_spatial_coherence must be in [0,1]")
        if not 0.0 <= float(self.minimum_peak_contrast) <= 1.0:
            raise ValueError("minimum_peak_contrast must be in [0,1]")
        if min(float(self.fusion_radius_deg), float(self.output_nms_radius_deg)) <= 0.0:
            raise ValueError("angular radii must be positive")
        if min(
            int(self.minimum_vote_count),
            int(self.minimum_frame_support),
            int(self.minimum_coarse_band_support),
            int(self.max_candidates),
        ) < 1:
            raise ValueError("support thresholds and candidate budget must be positive")

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


@dataclass(frozen=True)
class TfZoneVote:
    frame_index: int
    zone_index: int
    coarse_band_index: int
    center_frequency_hz: float
    azimuth_deg: float
    elevation_deg: float
    energy_excess_db: float
    spatial_coherence: float
    peak_contrast: float

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


@dataclass(frozen=True)
class TfZoneCandidate:
    azimuth_deg: float
    elevation_deg: float
    vote_count: int
    frame_support: int
    coarse_band_support: int
    zone_support: int
    weight_sum: float
    mean_coherence: float
    mean_energy_excess_db: float
    rank: int

    def to_dict(self) -> dict[str, float | int]:
        return asdict(self)


class TfZoneProjector:
    """Precomputed grid projector used for many one-second scenes."""

    def __init__(
        self,
        cfg: Mic16Config = Mic16Config(),
        grid: AzElGrid = AzElGrid(),
        config: TfZoneAnalysisConfig = TfZoneAnalysisConfig(),
    ) -> None:
        if float(config.frequency_max_hz) > float(cfg.sample_rate) / 2.0:
            raise ValueError("frequency range exceeds Nyquist")
        self.cfg, self.grid, self.config = cfg, grid, config
        frequency = np.fft.rfftfreq(int(cfg.fft_size), d=1.0 / float(cfg.sample_rate))
        self.frequency_bins = np.flatnonzero(
            (frequency >= float(config.frequency_min_hz))
            & (frequency <= float(config.frequency_max_hz))
        ).astype(np.int64)
        if self.frequency_bins.size == 0:
            raise ValueError("analysis range contains no FFT bins")
        self.frequencies_hz = frequency[self.frequency_bins]
        width_bins = max(
            1,
            int(round(float(config.zone_width_hz) / (float(cfg.sample_rate) / int(cfg.fft_size)))),
        )
        self.zone_slices = tuple(
            slice(start, min(start + width_bins, self.frequency_bins.size))
            for start in range(0, self.frequency_bins.size, width_bins)
        )
        signatures = build_phat_signatures(grid, cfg, self.frequency_bins)
        self.signatures = signatures.reshape(grid.point_count, compact32_pairs(cfg).shape[0], -1)
        self._azimuths = grid.azimuths_deg
        self._elevations = grid.elevations_deg
        self._pairs = compact32_pairs(cfg)

    def _frame_spectra(self, frame: np.ndarray) -> np.ndarray:
        values = np.asarray(frame, dtype=np.float64)
        if values.shape != (int(self.cfg.frame_samples), int(self.cfg.channels)):
            raise ValueError("frame shape does not match MIC16 configuration")
        if not np.isfinite(values).all():
            raise ValueError("frame must be finite")
        logical = apply_channel_map(values, default_channel_map(self.cfg), self.cfg)
        logical -= np.mean(logical, axis=0, keepdims=True)
        logical *= np.hanning(logical.shape[0])[:, None]
        return np.fft.rfft(logical, n=int(self.cfg.fft_size), axis=0)[self.frequency_bins]

    def _observation(self, spectra: np.ndarray) -> np.ndarray:
        cross = np.stack(
            [
                spectra[:, int(channel_a)] * np.conj(spectra[:, int(channel_b)])
                for channel_a, channel_b in self._pairs
            ],
            axis=0,
        )
        magnitude = np.abs(cross)
        return np.divide(
            cross,
            magnitude,
            out=np.zeros_like(cross),
            where=magnitude > float(self.cfg.phat_epsilon),
        ).astype(np.complex64)

    @staticmethod
    def _coarse_band(center_hz: float) -> int:
        if center_hz < 1500.0:
            return 0
        if center_hz < 3500.0:
            return 1
        return 2

    def extract_votes(self, frames: np.ndarray, noise_frames: np.ndarray) -> tuple[TfZoneVote, ...]:
        values = np.asarray(frames)
        noise = np.asarray(noise_frames)
        expected = (int(self.cfg.frame_samples), int(self.cfg.channels))
        if values.ndim != 3 or values.shape[1:] != expected or values.shape[0] < 1:
            raise ValueError("frames must have shape [positive_frame_count, frame_samples, channels]")
        if noise.ndim != 3 or noise.shape[1:] != expected or noise.shape[0] < 1:
            raise ValueError("noise_frames must match the frame shape")
        noise_zone_levels: list[list[float]] = [[] for _ in self.zone_slices]
        for frame in noise:
            spectra = self._frame_spectra(frame)
            power = np.mean(np.abs(spectra) ** 2, axis=1)
            for zone_index, zone in enumerate(self.zone_slices):
                noise_zone_levels[zone_index].append(
                    10.0 * np.log10(max(float(np.mean(power[zone])), 1e-20))
                )
        noise_floor = np.array([np.median(levels) for levels in noise_zone_levels])
        votes: list[TfZoneVote] = []
        for frame_index, frame in enumerate(values):
            spectra = self._frame_spectra(frame)
            observation = self._observation(spectra)
            power = np.mean(np.abs(spectra) ** 2, axis=1)
            for zone_index, zone in enumerate(self.zone_slices):
                signature = self.signatures[:, :, zone]
                observed = observation[:, zone]
                score = np.real(np.einsum("gpf,pf->g", np.conj(signature), observed))
                score /= float(signature.shape[1] * signature.shape[2])
                best_index = int(np.argmax(score))
                best_score = float(score[best_index])
                best_e, best_a = np.unravel_index(best_index, self.grid.shape)
                second = -np.inf
                for candidate_index, candidate_score in enumerate(score):
                    candidate_e, candidate_a = np.unravel_index(candidate_index, self.grid.shape)
                    if angular_distance_deg(
                        float(self._azimuths[best_a]),
                        float(self._elevations[best_e]),
                        float(self._azimuths[candidate_a]),
                        float(self._elevations[candidate_e]),
                    ) >= float(self.config.spatial_second_peak_nms_deg):
                        second = max(second, float(candidate_score))
                contrast = best_score - second if np.isfinite(second) else 0.0
                level = 10.0 * np.log10(max(float(np.mean(power[zone])), 1e-20))
                center_hz = float(np.mean(self.frequencies_hz[zone]))
                votes.append(
                    TfZoneVote(
                        frame_index=frame_index,
                        zone_index=zone_index,
                        coarse_band_index=self._coarse_band(center_hz),
                        center_frequency_hz=center_hz,
                        azimuth_deg=float(self._azimuths[best_a]),
                        elevation_deg=float(self._elevations[best_e]),
                        energy_excess_db=float(level - noise_floor[zone_index]),
                        spatial_coherence=best_score,
                        peak_contrast=max(float(contrast), 0.0),
                    )
                )
        return tuple(votes)


def select_tf_zone_candidates(
    votes: tuple[TfZoneVote, ...] | list[TfZoneVote],
    thresholds: TfZoneThresholds = TfZoneThresholds(),
) -> tuple[TfZoneCandidate, ...]:
    """Filter TF votes and form persistent anonymous direction clusters."""

    eligible = [
        vote
        for vote in votes
        if vote.energy_excess_db >= float(thresholds.minimum_energy_excess_db)
        and vote.spatial_coherence >= float(thresholds.minimum_spatial_coherence)
        and vote.peak_contrast >= float(thresholds.minimum_peak_contrast)
    ]
    seeds: list[tuple[TfZoneVote, list[TfZoneVote]]] = []
    unique_seeds = {
        (vote.azimuth_deg, vote.elevation_deg): vote for vote in eligible
    }
    for seed in unique_seeds.values():
        support = [
            vote
            for vote in eligible
            if angular_distance_deg(
                seed.azimuth_deg,
                seed.elevation_deg,
                vote.azimuth_deg,
                vote.elevation_deg,
            )
            <= float(thresholds.fusion_radius_deg)
        ]
        if len(support) < int(thresholds.minimum_vote_count):
            continue
        if len({vote.frame_index for vote in support}) < int(thresholds.minimum_frame_support):
            continue
        if len({vote.coarse_band_index for vote in support}) < int(
            thresholds.minimum_coarse_band_support
        ):
            continue
        seeds.append((seed, support))
    seeds.sort(
        key=lambda item: (
            len({vote.frame_index for vote in item[1]}),
            len({vote.coarse_band_index for vote in item[1]}),
            len(item[1]),
            sum(
                max(vote.spatial_coherence, 0.0)
                * max(vote.peak_contrast, 0.0)
                * max(vote.energy_excess_db, 0.0)
                for vote in item[1]
            ),
        ),
        reverse=True,
    )
    selected: list[TfZoneCandidate] = []
    for seed, support in seeds:
        weights = np.array(
            [
                max(vote.spatial_coherence, 0.0)
                * max(vote.peak_contrast, 0.0)
                * max(vote.energy_excess_db, 0.0)
                for vote in support
            ],
            dtype=np.float64,
        )
        if float(np.sum(weights)) <= 1e-12:
            weights = np.ones(len(support), dtype=np.float64)
        vectors = np.stack(
            [direction_unit_vector(vote.azimuth_deg, vote.elevation_deg).reshape(3) for vote in support],
            axis=0,
        ).astype(np.float64)
        centroid = np.sum(vectors * weights[:, None], axis=0)
        centroid /= max(float(np.linalg.norm(centroid)), 1e-12)
        candidate_azimuth = float(np.rad2deg(np.arctan2(centroid[0], centroid[2])))
        candidate_elevation = float(np.rad2deg(np.arcsin(np.clip(centroid[1], -1.0, 1.0))))
        if any(
            angular_distance_deg(
                candidate_azimuth,
                candidate_elevation,
                candidate.azimuth_deg,
                candidate.elevation_deg,
            )
            < float(thresholds.output_nms_radius_deg)
            for candidate in selected
        ):
            continue
        selected.append(
            TfZoneCandidate(
                azimuth_deg=candidate_azimuth,
                elevation_deg=candidate_elevation,
                vote_count=len(support),
                frame_support=len({vote.frame_index for vote in support}),
                coarse_band_support=len({vote.coarse_band_index for vote in support}),
                zone_support=len({vote.zone_index for vote in support}),
                weight_sum=float(np.sum(weights)),
                mean_coherence=float(np.mean([vote.spatial_coherence for vote in support])),
                mean_energy_excess_db=float(np.mean([vote.energy_excess_db for vote in support])),
                rank=len(selected) + 1,
            )
        )
        if len(selected) >= int(thresholds.max_candidates):
            break
    return tuple(selected)
