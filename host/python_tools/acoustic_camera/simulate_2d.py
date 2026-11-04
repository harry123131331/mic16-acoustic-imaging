"""Deterministic broadband PCM simulator for the MIC16 2-D research path."""

from __future__ import annotations

from dataclasses import asdict, dataclass

import numpy as np

from mic16_config import Mic16Config

from .geometry_2d import far_field_arrival_delays_samples_azel


SIMULATION_VERSION = "mic16_azel_broadband_farfield_nonideal_sim_v2"


@dataclass(frozen=True)
class ReflectionPath:
    """One coherent delayed path of the target probe."""

    azimuth_deg: float
    elevation_deg: float
    relative_gain: float
    extra_delay_samples: float

    def __post_init__(self) -> None:
        if not 0.0 <= float(self.relative_gain) <= 1.0:
            raise ValueError("reflection relative_gain must be in [0,1]")
        if float(self.extra_delay_samples) < 0.0:
            raise ValueError("reflection extra_delay_samples must be non-negative")


def fractional_delay_linear(signal: np.ndarray, delay_samples: float) -> np.ndarray:
    """Apply a non-circular fractional delay by linear interpolation."""

    values = np.asarray(signal, dtype=np.float32).reshape(-1)
    axis = np.arange(values.size, dtype=np.float32)
    return np.interp(
        axis - float(delay_samples),
        axis,
        values,
        left=0.0,
        right=0.0,
    ).astype(np.float32)


def band_limited_probe(
    sample_count: int,
    cfg: Mic16Config = Mic16Config(),
    *,
    seed: int = 23,
) -> np.ndarray:
    """Generate a repeatable speech-band-like broadband localization probe."""

    if int(sample_count) <= 0:
        raise ValueError("sample_count must be positive")
    rng = np.random.default_rng(int(seed))
    white = rng.standard_normal(int(sample_count)).astype(np.float64)
    spectrum = np.fft.rfft(white)
    frequencies = np.fft.rfftfreq(int(sample_count), d=1.0 / float(cfg.sample_rate))
    band = (frequencies >= float(cfg.bandpass_low_hz)) & (
        frequencies <= float(cfg.bandpass_high_hz)
    )
    # 温和的1/√f着色避免将白噪声当作语音，
    # 同时保留足够带宽形成清晰GCC-PHAT峰。
    coloration = np.zeros_like(frequencies)
    coloration[band] = 1.0 / np.sqrt(np.maximum(frequencies[band], 300.0))
    spectrum *= coloration
    probe = np.fft.irfft(spectrum, n=int(sample_count))
    probe -= float(np.mean(probe))
    probe /= float(np.sqrt(np.mean(np.square(probe))) + 1e-12)
    return probe.astype(np.float32)


def simulate_farfield_frame(
    azimuth_deg: float,
    elevation_deg: float,
    cfg: Mic16Config = Mic16Config(),
    *,
    snr_db: float = 10.0,
    seed: int = 23,
    nonideal_seed: int | None = None,
    channel_gain_std_db: float = 0.25,
    channel_delay_std_samples: float = 0.0,
    reflections: tuple[ReflectionPath, ...] = (),
    interferer_azimuth_deg: float | None = None,
    interferer_elevation_deg: float | None = None,
    sir_db: float | None = None,
) -> tuple[np.ndarray, dict[str, object]]:
    """Render one ``[frame_samples,channels]`` far-field PCM frame.

    The simulator is a controlled software experiment, not measured room data.
    It includes independent sensor noise, fixed coherent reflection paths, an
    optional independent broadband interferer, and per-channel gain/delay
    mismatch so result figures can state exactly what was exercised.
    """

    interferer_values = (
        interferer_azimuth_deg,
        interferer_elevation_deg,
        sir_db,
    )
    if any(value is not None for value in interferer_values) and not all(
        value is not None for value in interferer_values
    ):
        raise ValueError(
            "interferer azimuth, elevation and SIR must be supplied together"
        )
    rng = np.random.default_rng(
        int(seed) if nonideal_seed is None else int(nonideal_seed)
    )
    maximum_extra_delay = max(
        [float(path.extra_delay_samples) for path in reflections] or [0.0]
    )
    margin = max(128, int(np.ceil(maximum_extra_delay)) + 64)
    probe = band_limited_probe(int(cfg.frame_samples) + 2 * margin, cfg, seed=seed)
    delay_mismatch = rng.normal(
        0.0, max(0.0, float(channel_delay_std_samples)), int(cfg.channels)
    ).astype(np.float32)
    gains_db = rng.normal(
        0.0, max(0.0, float(channel_gain_std_db)), int(cfg.channels)
    )
    gains = np.power(10.0, gains_db / 20.0).astype(np.float32)

    def render_component(
        component_probe: np.ndarray,
        component_azimuth_deg: float,
        component_elevation_deg: float,
        *,
        extra_delay_samples: float = 0.0,
    ) -> np.ndarray:
        ideal_delays = far_field_arrival_delays_samples_azel(
            float(component_azimuth_deg), float(component_elevation_deg), cfg
        )
        applied_delays = ideal_delays + delay_mismatch
        # 公共延迟使各通道延迟非负而不改变通道对差；
        # 裁剪移除输出帧的插值边缘。
        applied_delays -= float(np.min(applied_delays))
        applied_delays += float(extra_delay_samples)
        channels = []
        for channel in range(int(cfg.channels)):
            delayed = fractional_delay_linear(
                component_probe, float(applied_delays[channel])
            )
            channels.append(delayed[margin : margin + int(cfg.frame_samples)])
        return np.stack(channels, axis=1).astype(np.float32)

    direct = render_component(probe, float(azimuth_deg), float(elevation_deg))
    target_with_multipath = direct.copy()
    for path in reflections:
        target_with_multipath += float(path.relative_gain) * render_component(
            probe,
            float(path.azimuth_deg),
            float(path.elevation_deg),
            extra_delay_samples=float(path.extra_delay_samples),
        )
    mixture = target_with_multipath
    measured_interferer_power = 0.0
    if all(value is not None for value in interferer_values):
        interferer_probe = band_limited_probe(
            int(cfg.frame_samples) + 2 * margin,
            cfg,
            seed=int(seed) + 10_000_019,
        )
        interferer = render_component(
            interferer_probe,
            float(interferer_azimuth_deg),
            float(interferer_elevation_deg),
        )
        direct_power_before_gain = float(np.mean(np.square(direct)))
        interferer_power_before_gain = float(np.mean(np.square(interferer)))
        interferer_scale = float(
            np.sqrt(
                direct_power_before_gain
                / (
                    max(10.0 ** (float(sir_db) / 10.0), 1e-12)
                    * max(interferer_power_before_gain, 1e-12)
                )
            )
        )
        interferer *= interferer_scale
        measured_interferer_power = float(np.mean(np.square(interferer * gains.reshape(1, -1))))
        mixture = mixture + interferer

    clean = (mixture * gains.reshape(1, -1)).astype(np.float32)
    direct_after_gain = direct * gains.reshape(1, -1)
    signal_power = float(np.mean(np.square(direct_after_gain)))
    noise_power = signal_power / max(10.0 ** (float(snr_db) / 10.0), 1e-12)
    noise = rng.standard_normal(clean.shape).astype(np.float32)
    noise *= float(np.sqrt(noise_power / (float(np.mean(np.square(noise))) + 1e-12)))
    frame = (clean + noise).astype(np.float32)
    metadata = {
        "simulation_version": SIMULATION_VERSION,
        "truth_azimuth_deg": float(azimuth_deg),
        "truth_elevation_deg": float(elevation_deg),
        "snr_db": float(snr_db),
        "seed": int(seed),
        "nonideal_seed": (
            int(seed) if nonideal_seed is None else int(nonideal_seed)
        ),
        "channel_gain_std_db": float(channel_gain_std_db),
        "channel_delay_std_samples": float(channel_delay_std_samples),
        "reflection_paths": [asdict(path) for path in reflections],
        "interferer_azimuth_deg": (
            None if interferer_azimuth_deg is None else float(interferer_azimuth_deg)
        ),
        "interferer_elevation_deg": (
            None if interferer_elevation_deg is None else float(interferer_elevation_deg)
        ),
        "sir_db": None if sir_db is None else float(sir_db),
        "measured_signal_power": signal_power,
        "measured_interferer_power": measured_interferer_power,
        "measured_noise_power": noise_power,
        "claim_boundary": "controlled synthetic far-field broadband PCM; not real-room or board evidence",
    }
    return frame, metadata
