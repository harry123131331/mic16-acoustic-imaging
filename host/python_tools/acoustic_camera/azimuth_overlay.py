"""Truthful one-dimensional azimuth overlay for camera-like backgrounds."""

from __future__ import annotations

import math
from dataclasses import asdict, dataclass
from pathlib import Path

import numpy as np


try:
    from PIL import Image, ImageDraw, ImageFont

    HAS_PIL = True
except Exception:  # noqa: BLE001 - caller receives an actionable error
    HAS_PIL = False


@dataclass(frozen=True)
class AzimuthOverlayState:
    angle_deg: float | None
    confidence: float
    valid: bool
    source: str
    frame_id: int | None = None
    top3_angles_deg: tuple[float, ...] = ()
    camera_center_deg: float = 90.0
    horizontal_fov_deg: float = 60.0
    confidence_threshold: float = 0.25
    algorithm_version: str = "--"
    parameter_version: str = "--"
    quality_text: str = ""
    demo_direction: bool = False


@dataclass(frozen=True)
class OverlayGeometry:
    visible: bool
    x_px: int | None
    relative_angle_deg: float | None
    offscreen_side: str | None
    top3_x_px: tuple[int, ...]

    def to_dict(self) -> dict[str, object]:
        return asdict(self)


def normalize_signed_deg(angle_deg: float) -> float:
    return (float(angle_deg) + 180.0) % 360.0 - 180.0


def azimuth_to_pixel(
    angle_deg: float,
    camera_center_deg: float,
    horizontal_fov_deg: float,
    width: int,
) -> tuple[int, bool, float]:
    """Map azimuth to a pinhole-like horizontal pixel coordinate."""
    width = max(2, int(width))
    fov = max(1.0, min(179.0, float(horizontal_fov_deg)))
    relative = normalize_signed_deg(float(angle_deg) - float(camera_center_deg))
    half = fov * 0.5
    visible = abs(relative) <= half
    denominator = math.tan(math.radians(half))
    normalized = 0.5 + 0.5 * math.tan(math.radians(relative)) / max(1.0e-9, denominator)
    x_px = int(round(normalized * (width - 1)))
    return max(0, min(width - 1, x_px)), visible, relative


def compute_overlay_geometry(
    state: AzimuthOverlayState,
    width: int,
) -> OverlayGeometry:
    if state.angle_deg is None:
        return OverlayGeometry(False, None, None, None, ())
    x_px, visible, relative = azimuth_to_pixel(
        state.angle_deg,
        state.camera_center_deg,
        state.horizontal_fov_deg,
        width,
    )
    display_valid = bool(state.valid) and float(state.confidence) >= float(state.confidence_threshold)
    offscreen_side = None
    if display_valid and not visible:
        offscreen_side = "left" if relative < 0 else "right"
    top3: list[int] = []
    if display_valid:
        for angle in state.top3_angles_deg:
            top_x, top_visible, _ = azimuth_to_pixel(
                angle,
                state.camera_center_deg,
                state.horizontal_fov_deg,
                width,
            )
            if top_visible:
                top3.append(top_x)
    return OverlayGeometry(display_valid and visible, x_px, relative, offscreen_side, tuple(top3))


def _font(size: int, bold: bool = False):
    if not HAS_PIL:
        raise RuntimeError("Pillow 未安装；声像融合页面需要 requirements-camera.txt")
    candidates = (
        Path("C:/Windows/Fonts/msyhbd.ttc") if bold else Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/segoeuib.ttf") if bold else Path("C:/Windows/Fonts/segoeui.ttf"),
        Path("C:/Windows/Fonts/arialbd.ttf") if bold else Path("C:/Windows/Fonts/arial.ttf"),
    )
    for candidate in candidates:
        if candidate.is_file():
            try:
                return ImageFont.truetype(str(candidate), size=size)
            except OSError:
                continue
    return ImageFont.load_default()


def _fit_to_size(rgb: np.ndarray, width: int, height: int):
    array = np.asarray(rgb, dtype=np.uint8)
    if array.ndim != 3 or array.shape[2] != 3:
        raise ValueError(f"expected RGB [H,W,3], got {array.shape}")
    image = Image.fromarray(array, mode="RGB")
    if image.size == (width, height):
        return image
    image.thumbnail((width, height), Image.Resampling.LANCZOS)
    canvas = Image.new("RGB", (width, height), (9, 16, 24))
    canvas.paste(image, ((width - image.width) // 2, (height - image.height) // 2))
    return canvas


def render_azimuth_overlay(
    rgb: np.ndarray,
    state: AzimuthOverlayState,
    *,
    width: int | None = None,
    height: int | None = None,
) -> tuple[np.ndarray, OverlayGeometry]:
    """Render a scientific 1-D direction band and return its geometry."""
    if not HAS_PIL:
        raise RuntimeError("Pillow 未安装；请安装 python_tools/requirements-camera.txt")
    array = np.asarray(rgb, dtype=np.uint8)
    target_width = max(320, int(width or array.shape[1]))
    target_height = max(180, int(height or array.shape[0]))
    image = _fit_to_size(array, target_width, target_height).convert("RGBA")
    geometry = compute_overlay_geometry(state, target_width)
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer, "RGBA")

    header_h = max(66, int(target_height * 0.105))
    footer_h = max(70, int(target_height * 0.115))
    plot_top = header_h
    plot_bottom = target_height - footer_h
    draw.rectangle((0, 0, target_width, header_h), fill=(5, 18, 28, 224))
    draw.rectangle((0, plot_bottom, target_width, target_height), fill=(5, 18, 28, 232))
    draw.line((0, plot_top, target_width, plot_top), fill=(87, 184, 225, 180), width=2)
    draw.line((0, plot_bottom, target_width, plot_bottom), fill=(87, 184, 225, 180), width=2)

    title_font = _font(max(18, target_height // 32), bold=True)
    body_font = _font(max(14, target_height // 46))
    small_font = _font(max(12, target_height // 56))
    draw.text((22, 12), "MIC16 声像融合观察窗", font=title_font, fill=(230, 244, 250, 255))
    badge = "1D AZIMUTH-ONLY · 非二维声学成像"
    badge_box = draw.textbbox((0, 0), badge, font=small_font)
    badge_w = badge_box[2] - badge_box[0]
    draw.rounded_rectangle(
        (target_width - badge_w - 38, 13, target_width - 18, 44),
        radius=8,
        fill=(158, 87, 20, 225),
    )
    draw.text((target_width - badge_w - 28, 17), badge, font=small_font, fill=(255, 247, 231, 255))

    center_x = target_width // 2
    draw.line((center_x, plot_top, center_x, plot_bottom), fill=(190, 220, 232, 70), width=1)
    draw.line(
        (0, (plot_top + plot_bottom) // 2, target_width, (plot_top + plot_bottom) // 2),
        fill=(190, 220, 232, 50),
        width=1,
    )
    half_fov = max(0.5, min(89.5, float(state.horizontal_fov_deg) * 0.5))
    for fraction in (0.0, 0.25, 0.5, 0.75, 1.0):
        x = int(round(fraction * (target_width - 1)))
        angle = normalize_signed_deg(
            float(state.camera_center_deg) + (fraction - 0.5) * 2.0 * half_fov
        )
        draw.line((x, plot_top, x, plot_top + 12), fill=(196, 224, 235, 130), width=1)
        label = f"{angle:+.0f}°"
        box = draw.textbbox((0, 0), label, font=small_font)
        draw.text(
            (max(2, min(target_width - (box[2] - box[0]) - 2, x - (box[2] - box[0]) // 2)), plot_top + 14),
            label,
            font=small_font,
            fill=(218, 235, 242, 215),
        )

    for rank, x in enumerate(geometry.top3_x_px[:3]):
        alpha = max(45, 110 - rank * 28)
        draw.line((x, plot_top + 42, x, plot_bottom), fill=(105, 207, 235, alpha), width=max(1, 3 - rank))

    if geometry.visible and geometry.x_px is not None:
        x = int(geometry.x_px)
        confidence = max(0.0, min(1.0, float(state.confidence)))
        band_half = max(14, int((1.08 - confidence) * target_width * 0.045))
        for offset in range(band_half, 0, -2):
            alpha = int(10 + 80 * (1.0 - offset / max(1, band_half)))
            draw.rectangle(
                (max(0, x - offset), plot_top + 38, min(target_width - 1, x + offset), plot_bottom),
                fill=(255, 178, 44, alpha),
            )
        draw.line((x, plot_top + 36, x, plot_bottom), fill=(255, 220, 126, 245), width=3)
        target_y = (plot_top + plot_bottom) // 2
        marker_r = max(13, target_height // 40)
        draw.ellipse(
            (x - marker_r, target_y - marker_r, x + marker_r, target_y + marker_r),
            outline=(255, 238, 179, 255),
            width=3,
        )
        draw.line((x - marker_r - 8, target_y, x + marker_r + 8, target_y), fill=(255, 238, 179, 230), width=2)
        draw.line((x, target_y - marker_r - 8, x, target_y + marker_r + 8), fill=(255, 238, 179, 230), width=2)
    elif geometry.offscreen_side:
        left = geometry.offscreen_side == "left"
        x = 18 if left else target_width - 18
        direction = "← 声源在视场左侧" if left else "声源在视场右侧 →"
        anchor = "la" if left else "ra"
        draw.text(
            (x, (plot_top + plot_bottom) // 2),
            direction,
            font=title_font,
            fill=(255, 193, 84, 255),
            anchor=anchor,
        )

    frame_text = "--" if state.frame_id is None else str(state.frame_id)
    angle_text = "--" if state.angle_deg is None else f"{float(state.angle_deg) % 360.0:.2f}°"
    valid = bool(state.valid) and float(state.confidence) >= float(state.confidence_threshold)
    if state.demo_direction:
        decision = "DEMO DIRECTION · 非算法结果"
        decision_color = (255, 193, 84, 255)
    elif valid:
        decision = "VALID LOCALIZATION"
        decision_color = (87, 214, 132, 255)
    elif state.angle_deg is None:
        decision = "WAITING FOR RESULT"
        decision_color = (174, 193, 204, 255)
    else:
        decision = "INVALID / LOW CONFIDENCE · 标记隐藏"
        decision_color = (255, 117, 117, 255)

    draw.text((22, plot_bottom + 12), decision, font=body_font, fill=decision_color)
    draw.text(
        (22, plot_bottom + 40),
        f"Azimuth {angle_text}   Confidence {float(state.confidence):.3f}   Frame {frame_text}",
        font=small_font,
        fill=(222, 238, 245, 245),
    )
    source_text = f"Source: {state.source or '--'}"
    source_box = draw.textbbox((0, 0), source_text, font=small_font)
    draw.text(
        (target_width - (source_box[2] - source_box[0]) - 22, plot_bottom + 16),
        source_text,
        font=small_font,
        fill=(190, 216, 228, 235),
    )
    version_text = f"Alg {state.algorithm_version} · Param {state.parameter_version}"
    version_box = draw.textbbox((0, 0), version_text, font=small_font)
    draw.text(
        (target_width - (version_box[2] - version_box[0]) - 22, plot_bottom + 43),
        version_text,
        font=small_font,
        fill=(145, 181, 198, 220),
    )

    result = Image.alpha_composite(image, layer).convert("RGB")
    return np.asarray(result, dtype=np.uint8), geometry
