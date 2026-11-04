# -*- coding: utf-8 -*-
"""
Build paper-style MIC16 SSL experiment outputs.

This script wraps mic16_algorithm_compare.py, then turns its CSV outputs and
optional neural-network evaluation CSVs into cleaner figures and tables.
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import subprocess
import sys
from pathlib import Path

import numpy as np

from mic16_config import (
    OFFICIAL_ARRAY_RADIUS_M,
    OFFICIAL_BANDPASS_HIGH_HZ,
    OFFICIAL_BANDPASS_LOW_HZ,
    OFFICIAL_CHANNELS,
    OFFICIAL_FRAME_SAMPLES,
    OFFICIAL_SAMPLE_RATE,
)


TOOL_DIR = Path(__file__).resolve().parent
PROJECT_DIR = TOOL_DIR.parent
WORKSPACE_DIR = PROJECT_DIR.parent

METHOD_DISPLAY = {
    "delay_and_sum": "Delay-and-Sum",
    "Delay-and-Sum": "Delay-and-Sum",
    "gcc_phat_tdoa": "GCC-PHAT/TDOA",
    "GCC-PHAT-TDOA": "GCC-PHAT/TDOA",
    "GCC-PHAT/TDOA": "GCC-PHAT/TDOA",
    "srp_phat": "SRP-PHAT",
    "SRP-PHAT": "SRP-PHAT",
    "GCC-CNN-CPU": "Legacy GCC-CNN (CPU)",
    "GCC-CNN-CUDA": "Legacy GCC-CNN (CUDA)",
}

SOURCE_DISPLAY = {
    "current_run": "Current run",
    "nn_eval": "LibriSpeech synthetic",
}

COLORS = {
    "Delay-and-Sum": "#2563EB",
    "GCC-PHAT/TDOA": "#7C3AED",
    "SRP-PHAT": "#059669",
    "Legacy GCC-CNN (CPU)": "#EA580C",
    "Legacy GCC-CNN (CUDA)": "#DC2626",
}

MARKERS = {
    "Delay-and-Sum": "o",
    "GCC-PHAT/TDOA": "s",
    "SRP-PHAT": "^",
    "Legacy GCC-CNN (CPU)": "D",
    "Legacy GCC-CNN (CUDA)": "v",
}

LINESTYLES = {
    "Delay-and-Sum": "-",
    "GCC-PHAT/TDOA": "--",
    "SRP-PHAT": "-.",
    "Legacy GCC-CNN (CPU)": ":",
    "Legacy GCC-CNN (CUDA)": "-",
}


def method_style(method: str) -> dict[str, object]:
    return {
        "color": COLORS.get(method, "#334155"),
        "marker": MARKERS.get(method, "o"),
        "linestyle": LINESTYLES.get(method, "-"),
    }


def set_angle_axis(ax, xmin: float = 0.0, xmax: float = 360.0) -> None:
    ax.set_xlim(xmin, xmax)
    ax.set_xticks(np.arange(0, 361, 45))
    ax.set_xticks(np.arange(0, 361, 15), minor=True)


def default_input_path() -> Path:
    sim = PROJECT_DIR / "matlab_tools" / "sim_mic16_circle_i32.bin"
    if sim.exists():
        return sim
    return WORKSPACE_DIR / "mic16_record_output" / "mic16_10s_raw_i32.npy"


def default_labels_for(input_path: Path) -> Path | None:
    sim_labels = PROJECT_DIR / "matlab_tools" / "sim_mic16_circle_frame_angles.csv"
    if input_path.name.startswith("sim_mic16_circle") and sim_labels.exists():
        return sim_labels
    return None


def default_comparison_csv() -> Path | None:
    path = WORKSPACE_DIR / "ssl_nn" / "outputs" / "eval_reports" / "method_comparison_librispeech_v2.csv"
    return path if path.exists() else None


def fnum(value: object, default: float = math.nan) -> float:
    try:
        if value is None or value == "":
            return default
        return float(value)
    except (TypeError, ValueError):
        return default


def display_method(name: str) -> str:
    return METHOD_DISPLAY.get(name, name.replace("_", " ").title())


def setup_matplotlib():
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    plt.rcParams.update({
        "figure.dpi": 140,
        "savefig.dpi": 320,
        "pdf.fonttype": 42,
        "ps.fonttype": 42,
        "svg.fonttype": "none",
        "font.family": "DejaVu Sans",
        "font.size": 9.0,
        "axes.titlesize": 10.5,
        "axes.labelsize": 9.5,
        "axes.titleweight": "semibold",
        "axes.grid": True,
        "grid.alpha": 0.22,
        "grid.linewidth": 0.6,
        "axes.edgecolor": "#475569",
        "axes.linewidth": 0.9,
        "axes.spines.top": False,
        "axes.spines.right": False,
        "legend.frameon": True,
        "legend.framealpha": 0.92,
        "legend.edgecolor": "#CBD5E1",
        "legend.facecolor": "white",
        "legend.fontsize": 8.5,
        "lines.linewidth": 1.9,
        "xtick.color": "#334155",
        "ytick.color": "#334155",
        "axes.labelcolor": "#0F172A",
        "figure.facecolor": "white",
        "axes.facecolor": "white",
    })
    return plt


def save_figure(fig, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(path, dpi=320, bbox_inches="tight", pad_inches=0.035)
    if path.suffix.lower() == ".png":
        fig.savefig(path.with_suffix(".pdf"), bbox_inches="tight", pad_inches=0.035)
        fig.savefig(path.with_suffix(".svg"), bbox_inches="tight", pad_inches=0.035)


def spectrum_to_db(spec: np.ndarray, floor_db: float = -60.0) -> np.ndarray:
    spec = np.asarray(spec, dtype=np.float64)
    peak = float(np.nanmax(np.abs(spec))) if spec.size else 0.0
    if peak <= 0.0 or not np.isfinite(peak):
        return np.full_like(spec, floor_db, dtype=np.float64)
    db = 20.0 * np.log10(np.maximum(np.abs(spec) / peak, 10.0 ** (floor_db / 20.0)))
    return np.clip(db, floor_db, 0.0)


def smooth_1d(values: np.ndarray, window: int = 3) -> np.ndarray:
    arr = np.asarray(values, dtype=np.float64)
    if window <= 1 or arr.size < window:
        return arr
    kernel = np.ones(window, dtype=np.float64) / float(window)
    pad = window // 2
    padded = np.r_[arr[-pad:], arr, arr[:pad]]
    return np.convolve(padded, kernel, mode="valid")[:arr.size]


def read_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        return list(csv.DictReader(f))


def read_metric_value_csv(path: Path) -> dict[str, float]:
    metrics: dict[str, float] = {}
    if not path.exists():
        return metrics
    with path.open("r", encoding="utf-8-sig", newline="") as f:
        for row in csv.DictReader(f):
            key = row.get("metric", "")
            if key:
                metrics[key] = fnum(row.get("value"))
    return metrics


def write_csv(path: Path, rows: list[dict[str, object]], fields: list[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({field: row.get(field, "") for field in fields})


def run_traditional_compare(args: argparse.Namespace, input_path: Path, label_path: Path | None, out_dir: Path) -> None:
    cmd = [
        sys.executable,
        str(TOOL_DIR / "mic16_algorithm_compare.py"),
        "--input", str(input_path),
        "--out-dir", str(out_dir),
        "--channels", str(args.channels),
        "--sample-rate", str(args.sample_rate),
        "--radius-m", str(args.radius_m),
        "--frame-len", str(args.frame_len),
        "--hop", str(args.hop),
        "--angle-step", str(args.angle_step),
        "--max-frames", str(args.max_frames),
        "--method", args.methods,
        "--bandpass-low", str(args.bandpass_low),
        "--bandpass-high", str(args.bandpass_high),
        "--diagnostic-seconds", str(args.diagnostic_seconds),
        "--diagnostic-max-freq", str(args.diagnostic_max_freq),
    ]
    if label_path is not None and label_path.exists():
        cmd += ["--labels", str(label_path)]
    cmd += ["--truth-angle-offset-deg", str(args.truth_angle_offset_deg)]
    if args.frontend_cic_comp:
        cmd.append("--frontend-cic-comp")
    if args.no_diagnostic_plots:
        cmd.append("--no-diagnostic-plots")
    if args.noise_profile:
        cmd += [
            "--noise-profile", args.noise_profile,
            "--noise-preprocess", args.noise_preprocess,
            "--noise-alpha", str(args.noise_alpha),
            "--noise-floor", str(args.noise_floor),
        ]
    if args.bad_channels:
        cmd += ["--bad-channels", args.bad_channels]
    if args.good_channels:
        cmd += ["--good-channels", args.good_channels]

    print("Running traditional SSL compare...", flush=True)
    print(" ".join(f'"{part}"' if " " in str(part) else str(part) for part in cmd), flush=True)
    subprocess.run(cmd, cwd=str(TOOL_DIR), check=True)


def load_traditional_metrics(traditional_dir: Path) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for summary in sorted(traditional_dir.glob("*_summary.csv")):
        stem = summary.stem.removesuffix("_summary")
        metrics = read_metric_value_csv(summary)
        if not metrics:
            continue
        row: dict[str, object] = {
            "source": "current_run",
            "method": display_method(stem),
        }
        row.update(metrics)
        rows.append(row)
    return rows


def load_nn_comparison(path: Path | None) -> list[dict[str, object]]:
    if path is None or not path.exists():
        return []
    rows: list[dict[str, object]] = []
    for row in read_csv_rows(path):
        out: dict[str, object] = {
            "source": "nn_eval",
            "method": display_method(row.get("method", "")),
        }
        for key, value in row.items():
            if key != "method":
                out[key] = fnum(value)
        rows.append(out)
    return rows


def normalize_metric_rows(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    fields = [
        "source", "method", "samples", "top1_accuracy", "top3_accuracy",
        "angle_mae_deg", "angle_rmse_deg", "angle_median_deg", "angle_p95_deg",
        "angle_max_deg", "within_5deg", "within_10deg", "within_15deg",
        "distance_top1_accuracy", "distance_mae_cm", "distance_rmse_cm",
        "distance_p95_cm", "joint_angle5_distance50",
    ]
    out: list[dict[str, object]] = []
    for row in rows:
        norm = {field: row.get(field, "") for field in fields}
        out.append(norm)
    return out


def write_metrics_markdown(path: Path, rows: list[dict[str, object]], preferred_source: str = "") -> bool:
    selected = select_metric_rows(rows, preferred_source=preferred_source)
    if not selected:
        return False
    lines = [
        "| Method | MAE (deg) | RMSE (deg) | P95 (deg) | Acc@5 | Acc@10 | Acc@15 |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for row in selected:
        def pct(key: str) -> str:
            value = fnum(row.get(key))
            if math.isnan(value):
                return ""
            return f"{value * 100.0:.1f}" if value <= 1.0001 else f"{value:.1f}"

        lines.append(
            f"| {row.get('method')} "
            f"| {fnum(row.get('angle_mae_deg')):.2f} "
            f"| {fnum(row.get('angle_rmse_deg')):.2f} "
            f"| {fnum(row.get('angle_p95_deg')):.2f} "
            f"| {pct('within_5deg')} "
            f"| {pct('within_10deg')} "
            f"| {pct('within_15deg')} |"
        )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return True


def write_metrics_latex(path: Path, rows: list[dict[str, object]], preferred_source: str = "") -> bool:
    selected = select_metric_rows(rows, preferred_source=preferred_source)
    if not selected:
        return False
    lines = [
        r"\begin{tabular}{lrrrrrr}",
        r"\hline",
        r"Method & MAE & RMSE & P95 & Acc@5 & Acc@10 & Acc@15 \\",
        r"\hline",
    ]
    for row in selected:
        def pct(key: str) -> str:
            value = fnum(row.get(key))
            if math.isnan(value):
                return "--"
            value = value * 100.0 if value <= 1.0001 else value
            return f"{value:.1f}"

        method = str(row.get("method", "")).replace("&", r"\&")
        lines.append(
            f"{method} & "
            f"{fnum(row.get('angle_mae_deg')):.2f} & "
            f"{fnum(row.get('angle_rmse_deg')):.2f} & "
            f"{fnum(row.get('angle_p95_deg')):.2f} & "
            f"{pct('within_5deg')} & "
            f"{pct('within_10deg')} & "
            f"{pct('within_15deg')} \\\\"
        )
    lines += [r"\hline", r"\end{tabular}"]
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return True


def load_spectrum(path: Path) -> tuple[np.ndarray, dict[str, np.ndarray]]:
    rows = read_csv_rows(path)
    if not rows:
        return np.asarray([], dtype=np.float32), {}
    angles = np.asarray([fnum(row.get("angle_deg")) for row in rows], dtype=np.float32)
    methods = [key for key in rows[0].keys() if key != "angle_deg"]
    spectra = {
        display_method(method): np.asarray([fnum(row.get(method)) for row in rows], dtype=np.float32)
        for method in methods
    }
    return angles, spectra


def circular_diff_deg(a: np.ndarray, b: float) -> np.ndarray:
    return np.abs((a - b + 180.0) % 360.0 - 180.0)


def half_power_width_deg(angles: np.ndarray, spec: np.ndarray, peak_idx: int) -> float:
    if angles.size < 2:
        return math.nan
    step = float(np.median(np.diff(angles)))
    threshold = 0.5 * float(spec[peak_idx])
    count = 1
    n = int(spec.size)
    left = (peak_idx - 1) % n
    while left != peak_idx and spec[left] >= threshold:
        count += 1
        left = (left - 1) % n
    right = (peak_idx + 1) % n
    while right != peak_idx and spec[right] >= threshold:
        count += 1
        right = (right + 1) % n
    return count * step


def analyze_spatial_peaks(angles: np.ndarray, spectra: dict[str, np.ndarray]) -> list[dict[str, object]]:
    if angles.size == 0 or not spectra:
        return []
    rows: list[dict[str, object]] = []
    label_offsets = [(-42, 16), (0, 32), (42, 16), (0, 46)]
    for plot_idx, (method, spec) in enumerate(spectra.items()):
        if spec.size == 0:
            continue
        peak_idx = int(np.nanargmax(spec))
        peak_angle = float(angles[peak_idx])
        peak_value = float(spec[peak_idx])
        exclude = circular_diff_deg(angles.astype(np.float64), peak_angle) <= 15.0
        candidates = np.where(~exclude)[0]
        if candidates.size:
            second_idx = int(candidates[np.nanargmax(spec[candidates])])
        else:
            second_idx = peak_idx
        second_value = float(spec[second_idx])
        psr_db = 20.0 * math.log10((peak_value + 1e-12) / (second_value + 1e-12))
        rows.append({
            "method": method,
            "peak_angle_deg": peak_angle,
            "peak_response": peak_value,
            "second_peak_angle_deg": float(angles[second_idx]),
            "second_peak_response": second_value,
            "peak_to_sidelobe_db": psr_db,
            "half_power_width_deg": half_power_width_deg(angles, spec, peak_idx),
        })
    return rows


def plot_peak_summary(path: Path, rows: list[dict[str, object]]) -> bool:
    if not rows:
        return False
    plt = setup_matplotlib()
    methods = [str(row["method"]) for row in rows]
    peak_angles = [fnum(row.get("peak_angle_deg"), 0.0) for row in rows]
    psr = [fnum(row.get("peak_to_sidelobe_db"), 0.0) for row in rows]
    widths = [fnum(row.get("half_power_width_deg"), 0.0) for row in rows]
    x = np.arange(len(rows), dtype=np.float32)

    fig, axes = plt.subplots(1, 3, figsize=(13.5, 4.4), constrained_layout=True)
    axes[0].bar(x, peak_angles, color="#2563EB")
    axes[0].set_ylabel("Peak angle / deg")
    axes[0].set_ylim(0, 360)
    axes[0].set_title("Main peak angle")

    axes[1].bar(x, psr, color="#059669")
    axes[1].set_ylabel("PSR / dB")
    axes[1].set_title("Peak-to-sidelobe ratio")

    axes[2].bar(x, widths, color="#EA580C")
    axes[2].set_ylabel("Width / deg")
    axes[2].set_title("Half-power width")

    for ax in axes:
        ax.set_xticks(x)
        ax.set_xticklabels(methods, rotation=30, ha="right")
    fig.suptitle("Spatial-spectrum peak diagnostics", fontsize=14, fontweight="bold")
    save_figure(fig, path)
    plt.close(fig)
    return True


def select_metric_rows(rows: list[dict[str, object]], preferred_source: str = "") -> list[dict[str, object]]:
    metric_rows = [
        row for row in rows
        if not math.isnan(fnum(row.get("angle_mae_deg")))
    ]
    if preferred_source:
        return [row for row in metric_rows if row.get("source") == preferred_source]
    nn_rows = [row for row in metric_rows if row.get("source") == "nn_eval"]
    if len(nn_rows) >= 2:
        return nn_rows
    return metric_rows


def plot_method_metrics(path: Path,
                        rows: list[dict[str, object]],
                        preferred_source: str = "",
                        title: str = "MIC16 SSL method comparison") -> bool:
    metric_rows = select_metric_rows(rows, preferred_source=preferred_source)
    if not metric_rows:
        return False
    plt = setup_matplotlib()

    source_count = len({str(row.get("source", "")) for row in metric_rows})
    if source_count == 1:
        labels = [str(row["method"]) for row in metric_rows]
        source_name = SOURCE_DISPLAY.get(str(metric_rows[0].get("source")), str(metric_rows[0].get("source")))
        subtitle = source_name
    else:
        labels = [
            f"{row['method']}\n{SOURCE_DISPLAY.get(str(row['source']), row['source'])}"
            for row in metric_rows
        ]
        subtitle = ""
    x = np.arange(len(metric_rows), dtype=np.float32)
    width = 0.24

    fig, axes = plt.subplots(1, 2, figsize=(12.0, 4.6), constrained_layout=True)
    err_keys = [("angle_mae_deg", "MAE"), ("angle_rmse_deg", "RMSE"), ("angle_p95_deg", "P95")]
    err_colors = ["#2563EB", "#059669", "#EA580C"]
    for offset, (key, label) in zip([-width, 0.0, width], err_keys):
        values = [fnum(row.get(key), 0.0) for row in metric_rows]
        axes[0].bar(x + offset, values, width=width, label=label,
                    color=err_colors[err_keys.index((key, label))], alpha=0.90)
    axes[0].set_title("Angle error comparison")
    axes[0].set_ylabel("Error / deg")
    axes[0].set_xticks(x)
    axes[0].set_xticklabels(labels, rotation=35, ha="right")
    axes[0].legend(ncols=3, loc="upper left")

    acc_keys = [("within_5deg", "Acc@5"), ("within_10deg", "Acc@10"), ("within_15deg", "Acc@15")]
    acc_colors = ["#7C3AED", "#0EA5E9", "#16A34A"]
    for offset, (key, label) in zip([-width, 0.0, width], acc_keys):
        values = []
        for row in metric_rows:
            value = fnum(row.get(key), 0.0)
            values.append(value * 100.0 if value <= 1.0001 else value)
        axes[1].bar(x + offset, values, width=width, label=label,
                    color=acc_colors[acc_keys.index((key, label))], alpha=0.90)
    axes[1].set_title("Angle accuracy comparison")
    axes[1].set_ylabel("Accuracy / %")
    axes[1].set_ylim(0, 105)
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(labels, rotation=35, ha="right")
    axes[1].legend(ncols=3, loc="lower right")

    fig.suptitle(title if not subtitle else f"{title} - {subtitle}", fontsize=12, fontweight="semibold")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_spectrum_line(path: Path, angles: np.ndarray, spectra: dict[str, np.ndarray]) -> bool:
    if angles.size == 0 or not spectra:
        return False
    plt = setup_matplotlib()
    methods = list(spectra.keys())
    fig, axes = plt.subplots(
        len(methods), 1,
        figsize=(11.5, 2.25 * len(methods) + 1.0),
        sharex=True,
        constrained_layout=True,
    )
    if len(methods) == 1:
        axes = [axes]

    for ax, method in zip(axes, methods):
        spec = spectra[method]
        style = method_style(method)
        color = str(style["color"])
        db = spectrum_to_db(spec, floor_db=-35.0)
        peak_idx = int(np.nanargmax(spec))
        peak_angle = float(angles[peak_idx])
        ax.plot(angles, db, color=color, linestyle=str(style["linestyle"]))
        ax.fill_between(angles, -35.0, db, color=color, alpha=0.10)
        ax.axvline(peak_angle, color=color, linestyle="--", linewidth=1.0, alpha=0.75)
        ax.scatter([peak_angle], [float(db[peak_idx])], s=34, color=color, zorder=4)
        ax.text(
            0.012, 0.78, method,
            transform=ax.transAxes,
            ha="left", va="center",
            fontsize=10.5,
            fontweight="semibold",
            color=color,
        )
        ax.text(
            0.985, 0.78, f"peak = {peak_angle:.0f} deg",
            transform=ax.transAxes,
            ha="right", va="center",
            fontsize=9.0,
            color=color,
            bbox={"boxstyle": "round,pad=0.25", "facecolor": "white", "edgecolor": "#E2E8F0", "alpha": 0.92},
        )
        ax.set_ylabel("Rel. / dB")
        set_angle_axis(ax)
        ax.set_ylim(-35.0, 1.0)
        ax.set_yticks([-30, -20, -10, 0])
    axes[-1].set_xlabel("Azimuth / deg")
    fig.suptitle("Average spatial spectrum by method", fontsize=12, fontweight="semibold")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_spectrum_polar(path: Path, angles: np.ndarray, spectra: dict[str, np.ndarray]) -> bool:
    if angles.size == 0 or not spectra:
        return False
    plt = setup_matplotlib()
    methods = list(spectra.keys())
    fig = plt.figure(figsize=(3.7 * len(methods), 4.25), constrained_layout=True)
    theta = np.deg2rad(np.r_[angles, angles[0]])

    for idx, method in enumerate(methods, start=1):
        ax = fig.add_subplot(1, len(methods), idx, projection="polar")
        spec = spectra[method]
        values = np.r_[spec, spec[0]]
        color = COLORS.get(method, None)
        peak_idx = int(np.nanargmax(spec))
        peak_angle = float(angles[peak_idx])
        ax.plot(theta, values, color=color, linewidth=2.2)
        ax.fill(theta, values, color=color, alpha=0.10)
        peak_idx = int(np.nanargmax(spec))
        ax.scatter([math.radians(float(angles[peak_idx]))], [float(spec[peak_idx])],
                   s=42, color=color, zorder=5)
        ax.set_theta_zero_location("E")
        ax.set_theta_direction(1)
        ax.set_rlim(0.0, 1.05)
        ax.set_rticks([0.5, 1.0])
        ax.tick_params(labelsize=8)
        ax.grid(color="#CBD5E1", alpha=0.55, linewidth=0.8)
        ax.set_title(f"{method}\npeak {peak_angle:.0f} deg", pad=12, fontsize=10.5, color=color)
    fig.suptitle("Average spatial spectrum (polar)", fontsize=13, fontweight="semibold")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_spectrum_heatmap(path: Path, angles: np.ndarray, spectra: dict[str, np.ndarray]) -> bool:
    if angles.size == 0 or not spectra:
        return False
    plt = setup_matplotlib()
    methods = list(spectra.keys())
    matrix = np.vstack([spectrum_to_db(spectra[method], floor_db=-30.0) for method in methods])
    fig, ax = plt.subplots(figsize=(9.0, 0.72 * len(methods) + 2.2), constrained_layout=True)
    im = ax.imshow(matrix, aspect="auto", interpolation="nearest", cmap="viridis", vmin=-30.0, vmax=0.0,
                   extent=[float(angles[0]), float(angles[-1]), len(methods) - 0.5, -0.5])
    ax.set_yticks(np.arange(len(methods)))
    ax.set_yticklabels(methods)
    set_angle_axis(ax, float(angles[0]), float(angles[-1]))
    ax.set_xlabel("Azimuth / deg")
    ax.set_title("Spatial spectrum heatmap")
    cbar = fig.colorbar(im, ax=ax, pad=0.01)
    cbar.set_label("Relative response / dB")
    for idx, method in enumerate(methods):
        peak = int(np.nanargmax(spectra[method]))
        ax.scatter([angles[peak]], [idx], color="white", edgecolor="black", s=38, zorder=4)
        ax.text(angles[peak] + 3, idx, f"{angles[peak]:.0f}", color="white",
                va="center", fontsize=8)
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_publication_spectrum_db(path: Path, angles: np.ndarray, spectra: dict[str, np.ndarray]) -> bool:
    if angles.size == 0 or not spectra:
        return False
    plt = setup_matplotlib()
    fig, ax = plt.subplots(figsize=(7.2, 4.3), constrained_layout=True)
    markevery = max(1, int(round(len(angles) / 12)))

    label_offsets = [(-18, 9), (0, 19), (18, 9), (0, 29)]
    for plot_idx, (method, spec) in enumerate(spectra.items()):
        style = method_style(method)
        color = str(style["color"])
        db = smooth_1d(spectrum_to_db(spec, floor_db=-35.0), window=3)
        ax.plot(
            angles, db,
            label=method,
            color=color,
            linestyle=str(style["linestyle"]),
            marker=str(style["marker"]),
            markersize=4.0,
            markevery=markevery,
            linewidth=1.75,
        )
        peak_idx = int(np.nanargmax(spec))
        peak_angle = float(angles[peak_idx])
        peak_db = float(db[peak_idx])
        ax.scatter([peak_angle], [peak_db], s=36, color=color, edgecolor="white", linewidth=0.7, zorder=4)
        dx, dy = label_offsets[plot_idx % len(label_offsets)]
        ax.annotate(
            f"{peak_angle:.0f} deg",
            xy=(peak_angle, peak_db),
            xytext=(dx, dy),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=8.0,
            color=color,
            bbox={"boxstyle": "round,pad=0.12", "facecolor": "white", "edgecolor": "none", "alpha": 0.72},
            arrowprops={"arrowstyle": "-", "color": color, "lw": 0.6, "alpha": 0.65},
            clip_on=False,
        )

    set_angle_axis(ax, float(np.nanmin(angles)), float(np.nanmax(angles)))
    ax.set_ylim(-35, 4)
    ax.set_xlabel("Azimuth / deg")
    ax.set_ylabel("Relative spatial spectrum / dB")
    ax.legend(
        loc="upper center",
        bbox_to_anchor=(0.5, 1.11),
        ncols=min(3, len(spectra)),
        borderaxespad=0.0,
    )
    ax.grid(True, which="major", color="#94A3B8", alpha=0.35, linewidth=0.65)
    ax.minorticks_on()
    ax.grid(True, which="minor", color="#CBD5E1", alpha=0.18, linewidth=0.45)
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_publication_spectrum_surface(path: Path, angles: np.ndarray, spectra: dict[str, np.ndarray]) -> bool:
    if angles.size == 0 or not spectra:
        return False
    plt = setup_matplotlib()
    methods = list(spectra.keys())
    z_rows = [smooth_1d(spectrum_to_db(np.asarray(spectra[method], dtype=np.float64), floor_db=-30.0), window=3)
              for method in methods]
    z = np.vstack(z_rows)
    x, y = np.meshgrid(angles.astype(np.float64), np.arange(len(methods), dtype=np.float64))

    fig = plt.figure(figsize=(8.2, 5.6))
    ax = fig.add_subplot(111, projection="3d")
    surf = ax.plot_surface(
        x, y, z,
        cmap="viridis",
        rstride=1,
        cstride=1,
        linewidth=0.12,
        edgecolor=(0.25, 0.25, 0.25, 0.20),
        antialiased=True,
        alpha=0.96,
    )
    ax.set_xlabel("Azimuth / deg", labelpad=8)
    ax.set_ylabel("Algorithm", labelpad=8)
    ax.set_zlabel("Response / dB", labelpad=7)
    ax.set_xlim(float(np.nanmin(angles)), float(np.nanmax(angles)))
    ax.set_ylim(-0.2, max(0.2, len(methods) - 0.8))
    ax.set_zlim(-30.0, 1.0)
    ax.set_yticks(np.arange(len(methods)))
    ax.set_yticklabels(methods, fontsize=8)
    ax.view_init(elev=28, azim=-58)
    ax.set_title("Spatial spectrum surface across algorithms", pad=14)
    fig.subplots_adjust(left=0.02, right=0.86, top=0.90, bottom=0.03)
    fig.colorbar(surf, ax=ax, shrink=0.62, pad=0.08, label="Relative response / dB")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_channel_health(path: Path, quality_csv: Path) -> bool:
    rows = read_csv_rows(quality_csv)
    if not rows:
        return False
    plt = setup_matplotlib()
    ch = np.asarray([int(fnum(row.get("channel"), 0)) for row in rows])
    rel = np.asarray([fnum(row.get("relative_rms_db"), -120.0) for row in rows])
    used = np.asarray([int(fnum(row.get("used_by_algorithm"), 0)) for row in rows], dtype=bool)
    colors = np.where(used, "#2563EB", "#DC2626")
    fig, ax = plt.subplots(figsize=(10, 4.2), constrained_layout=True)
    ax.bar(ch, rel, color=colors, alpha=0.88)
    ax.axhline(-20.0, color="#64748B", linestyle="--", linewidth=1.1, label="-20 dB reference")
    ax.set_xticks(ch)
    ax.set_xlabel("Channel")
    ax.set_ylabel("Relative RMS / dB")
    ax.set_title("Channel health and active-channel selection")
    ax.legend(loc="lower right")
    for pos, (idx, ok) in enumerate(zip(ch, used)):
        ax.text(idx, rel[pos] + 0.8, "used" if ok else "rej", ha="center", fontsize=7,
                color="#1E293B")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_error_cdf(path: Path, compare_csv: Path) -> bool:
    rows = read_csv_rows(compare_csv)
    if not rows or not rows[0].get("abs_error_deg"):
        return False
    grouped: dict[str, list[float]] = {}
    for row in rows:
        err = fnum(row.get("abs_error_deg"))
        if math.isnan(err):
            continue
        grouped.setdefault(display_method(row.get("method", "")), []).append(err)
    if not grouped:
        return False
    plt = setup_matplotlib()
    all_errors = np.asarray([err for errs in grouped.values() for err in errs], dtype=np.float32)
    finite_errors = all_errors[np.isfinite(all_errors)]
    if finite_errors.size == 0:
        return False
    p98 = float(np.percentile(finite_errors, 98))
    x_max = min(180.0, max(15.0, math.ceil((p98 + 1e-6) / 5.0) * 5.0 + 5.0))

    fig, ax = plt.subplots(figsize=(7.2, 4.4), constrained_layout=True)
    for method, errs in grouped.items():
        arr = np.sort(np.asarray(errs, dtype=np.float32))
        cdf = np.arange(1, arr.size + 1, dtype=np.float32) / arr.size
        style = method_style(method)
        xvals = np.r_[0.0, arr, x_max]
        yvals = np.r_[0.0, cdf * 100.0, 100.0]
        ax.step(
            xvals, yvals,
            where="post",
            label=method,
            color=str(style["color"]),
            linestyle=str(style["linestyle"]),
            linewidth=1.9,
        )
    for threshold in [5, 10, 15]:
        if threshold <= x_max:
            ax.axvline(threshold, color="#64748B", linestyle="--", linewidth=0.9, alpha=0.85)
            ax.text(threshold + 0.3, 4.0, f"{threshold}",
                    rotation=90,
                    va="bottom", ha="left", fontsize=8, color="#64748B")
    ax.set_xlabel("Absolute angle error / deg")
    ax.set_ylabel("CDF / %")
    ax.set_title("Angle-error CDF")
    ax.set_xlim(0, x_max)
    ax.set_ylim(0, 101)
    ax.set_yticks([0, 25, 50, 75, 100])
    ax.legend(loc="lower right")
    save_figure(fig, path)
    plt.close(fig)
    return True


def plot_prediction_timeline(path: Path, compare_csv: Path) -> bool:
    rows = read_csv_rows(compare_csv)
    labeled = [row for row in rows if row.get("truth_angle_deg", "") != ""]
    if not labeled:
        return False
    plt = setup_matplotlib()
    methods = sorted({display_method(row.get("method", "")) for row in labeled})
    fig, ax = plt.subplots(figsize=(12, 5.0), constrained_layout=True)

    truth_by_frame: dict[int, float] = {}
    method_points: dict[str, list[tuple[int, float]]] = {method: [] for method in methods}
    for row in labeled:
        frame = int(fnum(row.get("frame_index"), 0))
        truth_by_frame[frame] = fnum(row.get("truth_angle_deg"))
        method_points[display_method(row.get("method", ""))].append((frame, fnum(row.get("angle_deg"))))

    frames = np.asarray(sorted(truth_by_frame), dtype=np.int32)
    truth = np.asarray([truth_by_frame[int(frame)] for frame in frames], dtype=np.float32)
    ax.plot(frames, truth, color="#111827", linewidth=2.2, linestyle="--", label="Ground truth")
    for method, points in method_points.items():
        points = sorted(points)
        style = method_style(method)
        ax.plot([p[0] for p in points], [p[1] for p in points],
                marker="o", markersize=2.5, linewidth=1.3,
                label=method, color=str(style["color"]), alpha=0.82)
    ax.set_xlabel("Frame index")
    ax.set_ylabel("Azimuth / deg")
    ax.set_ylim(-5, 365)
    ax.set_yticks(np.arange(0, 361, 45))
    ax.set_title("Prediction timeline")
    ax.legend(loc="upper right", ncols=2)
    save_figure(fig, path)
    plt.close(fig)
    return True


def write_markdown_summary(path: Path,
                           args: argparse.Namespace,
                           input_path: Path,
                           label_path: Path | None,
                           metric_rows: list[dict[str, object]],
                           generated: list[Path]) -> None:
    lines = [
        "# MIC16 paper-style experiment report",
        "",
        "## Input",
        f"- Audio input: `{input_path}`",
        f"- Label CSV: `{label_path if label_path else 'none'}`",
        f"- Sample rate: `{args.sample_rate}` Hz",
        f"- Frame length / hop: `{args.frame_len}` / `{args.hop}` samples",
        f"- Angle step: `{args.angle_step}` deg",
        f"- Truth angle offset: `{args.truth_angle_offset_deg}` deg",
        f"- Bandpass: `{args.bandpass_low}` - `{args.bandpass_high}` Hz",
        f"- Max frames: `{args.max_frames}`",
        "",
        "## Interpretation Notes",
        "- If labels are available, MAE/RMSE/Acc@k are computed against ground-truth azimuth.",
        "- If labels are not available, use the channel-health and spatial-spectrum figures for diagnosis.",
        "- A narrow and single dominant spectrum peak is better for controlled single-source tests.",
        "- Strong side lobes or unstable peaks usually indicate low SNR, bad channels, reverberation, or array geometry mismatch.",
        "",
        "## Generated Files",
    ]
    for item in generated:
        lines.append(f"- `{item}`")
    if metric_rows:
        lines += ["", "## Key Metrics", ""]
        lines.append("| Source | Method | MAE(deg) | RMSE(deg) | P95(deg) | Acc@5 | Acc@10 | Acc@15 |")
        lines.append("|---|---:|---:|---:|---:|---:|---:|---:|")
        for row in metric_rows:
            def pct(key: str) -> str:
                value = fnum(row.get(key))
                if math.isnan(value):
                    return ""
                return f"{value * 100.0:.1f}%" if value <= 1.0001 else f"{value:.1f}%"

            lines.append(
                f"| {SOURCE_DISPLAY.get(str(row.get('source')), row.get('source'))} "
                f"| {row.get('method')} "
                f"| {fnum(row.get('angle_mae_deg')):.2f} "
                f"| {fnum(row.get('angle_rmse_deg')):.2f} "
                f"| {fnum(row.get('angle_p95_deg')):.2f} "
                f"| {pct('within_5deg')} "
                f"| {pct('within_10deg')} "
                f"| {pct('within_15deg')} |"
            )
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Create paper-style MIC16 SSL figures and comparison tables.")
    parser.add_argument("--input", default="", help="16-channel PCM recording/simulation .npy/.bin")
    parser.add_argument("--labels", default="", help="Optional frame label CSV")
    parser.add_argument("--truth-angle-offset-deg", type=float, default=None,
                        help="Label-angle offset before metrics; default is 180 deg for legacy MATLAB sim, else 0 deg")
    parser.add_argument("--out-dir", default=str(TOOL_DIR / "results" / "paper_experiment"))
    parser.add_argument("--comparison-csv", default="", help="Optional NN/traditional comparison CSV")
    parser.add_argument("--channels", type=int, default=OFFICIAL_CHANNELS)
    parser.add_argument("--sample-rate", type=float, default=OFFICIAL_SAMPLE_RATE)
    parser.add_argument("--radius-m", type=float, default=OFFICIAL_ARRAY_RADIUS_M)
    parser.add_argument("--frame-len", type=int, default=OFFICIAL_FRAME_SAMPLES)
    parser.add_argument("--hop", type=int, default=OFFICIAL_FRAME_SAMPLES)
    parser.add_argument("--angle-step", type=float, default=5.0)
    parser.add_argument("--max-frames", type=int, default=200)
    parser.add_argument("--methods", choices=["all", "gcc-tdoa", "srp-phat", "das"], default="all")
    parser.add_argument("--bandpass-low", type=float, default=OFFICIAL_BANDPASS_LOW_HZ)
    parser.add_argument("--bandpass-high", type=float, default=OFFICIAL_BANDPASS_HIGH_HZ)
    parser.add_argument("--diagnostic-seconds", type=float, default=0.12)
    parser.add_argument("--diagnostic-max-freq", type=float, default=10000.0)
    parser.add_argument("--frontend-cic-comp", action="store_true")
    parser.add_argument("--noise-profile", default="")
    parser.add_argument("--noise-preprocess", choices=["none", "shared-wiener", "per-channel-wiener"],
                        default="shared-wiener")
    parser.add_argument("--noise-alpha", type=float, default=0.8)
    parser.add_argument("--noise-floor", type=float, default=0.2)
    parser.add_argument("--bad-channels", default="")
    parser.add_argument("--good-channels", default="")
    parser.add_argument("--no-run-compare", action="store_true")
    parser.add_argument("--no-diagnostic-plots", action="store_true")
    args = parser.parse_args()

    input_path = (Path(args.input) if args.input else default_input_path()).resolve()
    label_path = Path(args.labels).resolve() if args.labels else default_labels_for(input_path)
    if label_path is not None:
        label_path = label_path.resolve()
    if args.truth_angle_offset_deg is None:
        if label_path is not None and label_path.name == "sim_mic16_circle_frame_angles.csv":
            args.truth_angle_offset_deg = 180.0
        else:
            args.truth_angle_offset_deg = 0.0
    comparison_path = Path(args.comparison_csv).resolve() if args.comparison_csv else default_comparison_csv()
    if comparison_path is not None:
        comparison_path = comparison_path.resolve()
    out_dir = Path(args.out_dir).resolve()
    traditional_dir = out_dir / "traditional_ssl"
    figures_dir = out_dir / "paper_figures"
    figures_dir.mkdir(parents=True, exist_ok=True)

    if not input_path.exists():
        raise FileNotFoundError(f"input file does not exist: {input_path}")
    if label_path is not None and not label_path.exists():
        raise FileNotFoundError(f"label file does not exist: {label_path}")

    if not args.no_run_compare:
        run_traditional_compare(args, input_path, label_path, traditional_dir)

    metric_rows = load_traditional_metrics(traditional_dir)
    metric_rows.extend(load_nn_comparison(comparison_path))
    metric_rows = normalize_metric_rows(metric_rows)

    metrics_table = out_dir / "paper_metrics_table.csv"
    metric_fields = [
        "source", "method", "samples", "top1_accuracy", "top3_accuracy",
        "angle_mae_deg", "angle_rmse_deg", "angle_median_deg", "angle_p95_deg",
        "angle_max_deg", "within_5deg", "within_10deg", "within_15deg",
        "distance_top1_accuracy", "distance_mae_cm", "distance_rmse_cm",
        "distance_p95_cm", "joint_angle5_distance50",
    ]
    write_csv(metrics_table, metric_rows, metric_fields)

    generated: list[Path] = [metrics_table]
    spectrum_csv = traditional_dir / "traditional_ssl_spectrum.csv"
    compare_csv = traditional_dir / "traditional_ssl_compare.csv"
    quality_csv = traditional_dir / "channel_quality.csv"
    angles, spectra = load_spectrum(spectrum_csv)
    peak_rows = analyze_spatial_peaks(angles, spectra)

    peak_table = out_dir / "paper_spatial_peak_summary.csv"
    if peak_rows:
        write_csv(
            peak_table,
            peak_rows,
            [
                "method", "peak_angle_deg", "peak_response",
                "second_peak_angle_deg", "second_peak_response",
                "peak_to_sidelobe_db", "half_power_width_deg",
            ],
        )
        generated.append(peak_table)

    metrics_md = out_dir / "paper_metrics_table.md"
    if write_metrics_markdown(metrics_md, metric_rows):
        generated.append(metrics_md)
    metrics_tex = out_dir / "paper_metrics_table.tex"
    if write_metrics_latex(metrics_tex, metric_rows):
        generated.append(metrics_tex)

    figure_jobs = [
        (figures_dir / "paper_method_metrics.png", lambda p: plot_method_metrics(p, metric_rows)),
        (figures_dir / "paper_current_run_metrics.png",
         lambda p: plot_method_metrics(p, metric_rows, preferred_source="current_run",
                                       title="Current-run SSL metrics")),
        (figures_dir / "paper_spatial_spectrum_line.png", lambda p: plot_spectrum_line(p, angles, spectra)),
        (figures_dir / "paper_spatial_spectrum_polar.png", lambda p: plot_spectrum_polar(p, angles, spectra)),
        (figures_dir / "paper_spatial_spectrum_heatmap.png", lambda p: plot_spectrum_heatmap(p, angles, spectra)),
        (figures_dir / "paper_pub_spatial_spectrum_db.png", lambda p: plot_publication_spectrum_db(p, angles, spectra)),
        (figures_dir / "paper_pub_spatial_spectrum_surface.png", lambda p: plot_publication_spectrum_surface(p, angles, spectra)),
        (figures_dir / "paper_spatial_peak_summary.png", lambda p: plot_peak_summary(p, peak_rows)),
        (figures_dir / "paper_channel_health.png", lambda p: plot_channel_health(p, quality_csv)),
        (figures_dir / "paper_error_cdf.png", lambda p: plot_error_cdf(p, compare_csv)),
        (figures_dir / "paper_prediction_timeline.png", lambda p: plot_prediction_timeline(p, compare_csv)),
    ]
    for path, job in figure_jobs:
        try:
            if job(path):
                generated.append(path)
                pdf_path = path.with_suffix(".pdf")
                if pdf_path.exists():
                    generated.append(pdf_path)
                svg_path = path.with_suffix(".svg")
                if svg_path.exists():
                    generated.append(svg_path)
        except Exception as exc:  # 部分实验未完成时仍可生成完整报告。
            print(f"warning: failed to generate {path.name}: {exc}")

    manifest = {
        "input": str(input_path),
        "labels": "" if label_path is None else str(label_path),
        "comparison_csv": "" if comparison_path is None else str(comparison_path),
        "traditional_dir": str(traditional_dir),
        "figures_dir": str(figures_dir),
        "generated": [str(path) for path in generated],
        "args": vars(args),
    }
    manifest_path = out_dir / "paper_experiment_manifest.json"
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    generated.append(manifest_path)

    summary_path = out_dir / "paper_experiment_summary.md"
    write_markdown_summary(summary_path, args, input_path, label_path, metric_rows, generated)
    generated.append(summary_path)

    print("=" * 72)
    print(f"Paper experiment output: {out_dir}")
    for path in generated:
        print(f"generated: {path}")


if __name__ == "__main__":
    main()
