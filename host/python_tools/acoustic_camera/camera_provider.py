"""Camera/video/static/demo frame providers for the Phase-1 GUI.

Only the provider worker touches blocking video APIs.  Tk widgets must consume
CameraFrame objects from CameraWorker.events on the GUI thread.
"""

from __future__ import annotations

import queue
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

import numpy as np


CAMERA_SOURCE_KINDS = ("科研演示背景", "静态图像", "视频文件", "USB 摄像头")


try:
    from PIL import Image, ImageDraw, ImageFont

    HAS_PIL = True
except Exception:  # noqa: BLE001 - the core GUI remains usable without camera extras
    HAS_PIL = False


def _import_cv2():
    try:
        import cv2

        return cv2
    except Exception as exc:  # noqa: BLE001 - converted to an actionable optional-dependency error
        raise RuntimeError(
            "OpenCV 未安装；USB 摄像头和视频文件需要先安装 "
            "python_tools/requirements-camera.txt。静态图和科研演示背景仍可使用。"
        ) from exc


def opencv_available() -> bool:
    try:
        _import_cv2()
        return True
    except RuntimeError:
        return False


@dataclass(frozen=True)
class CameraFrame:
    rgb: np.ndarray
    source_kind: str
    source_label: str
    frame_index: int
    captured_at: float


class FrameProvider(Protocol):
    fps_hint: float

    def open(self) -> None:
        ...

    def read(self) -> CameraFrame | None:
        ...

    def close(self) -> None:
        ...


def _font(size: int):
    if not HAS_PIL:
        return None
    for candidate in (
        Path("C:/Windows/Fonts/msyh.ttc"),
        Path("C:/Windows/Fonts/segoeui.ttf"),
        Path("C:/Windows/Fonts/arial.ttf"),
    ):
        if candidate.is_file():
            try:
                return ImageFont.truetype(str(candidate), size=size)
            except OSError:
                continue
    return ImageFont.load_default()


def make_demo_frame(
    width: int = 1280,
    height: int = 720,
    *,
    frame_index: int = 0,
) -> np.ndarray:
    """Generate a deterministic laboratory-style background without hardware."""
    width = max(320, int(width))
    height = max(180, int(height))
    yy, xx = np.mgrid[0:height, 0:width]
    vertical = yy.astype(np.float32) / max(1, height - 1)
    horizontal = xx.astype(np.float32) / max(1, width - 1)
    rgb = np.empty((height, width, 3), dtype=np.uint8)
    rgb[..., 0] = np.clip(19 + 18 * vertical + 8 * horizontal, 0, 255)
    rgb[..., 1] = np.clip(33 + 24 * vertical + 10 * horizontal, 0, 255)
    rgb[..., 2] = np.clip(48 + 34 * vertical + 16 * horizontal, 0, 255)

    if not HAS_PIL:
        for x in range(0, width, max(40, width // 16)):
            rgb[:, x : x + 1, :] = (61, 83, 101)
        for y in range(0, height, max(40, height // 9)):
            rgb[y : y + 1, :, :] = (61, 83, 101)
        return rgb

    image = Image.fromarray(rgb, mode="RGB")
    draw = ImageDraw.Draw(image, "RGBA")
    grid_x = max(48, width // 16)
    grid_y = max(48, height // 9)
    for x in range(0, width, grid_x):
        draw.line((x, 0, x, height), fill=(100, 139, 160, 35), width=1)
    for y in range(0, height, grid_y):
        draw.line((0, y, width, y), fill=(100, 139, 160, 35), width=1)

    horizon = int(height * 0.60)
    draw.line((0, horizon, width, horizon), fill=(118, 159, 180, 95), width=2)
    draw.polygon(
        (
            (int(width * 0.10), horizon),
            (int(width * 0.90), horizon),
            (width, height),
            (0, height),
        ),
        fill=(8, 18, 27, 105),
    )
    center_x = width // 2
    center_y = int(height * 0.47)
    radius = max(44, int(min(width, height) * 0.075))
    draw.ellipse(
        (center_x - radius, center_y - radius, center_x + radius, center_y + radius),
        outline=(112, 189, 224, 190),
        width=3,
    )
    for index in range(16):
        theta = 2.0 * np.pi * index / 16.0
        mx = center_x + int(radius * np.cos(theta))
        my = center_y - int(radius * np.sin(theta))
        draw.ellipse((mx - 4, my - 4, mx + 4, my + 4), fill=(147, 219, 246, 220))
    draw.line((center_x - 18, center_y, center_x + 18, center_y), fill=(147, 219, 246, 150), width=2)
    draw.line((center_x, center_y - 18, center_x, center_y + 18), fill=(147, 219, 246, 150), width=2)

    pulse = 0.5 + 0.5 * np.sin(float(frame_index) * 0.18)
    source_x = int(width * (0.73 + 0.015 * pulse))
    source_y = int(height * 0.40)
    source_r = 16 + int(4 * pulse)
    draw.ellipse(
        (source_x - source_r, source_y - source_r, source_x + source_r, source_y + source_r),
        fill=(255, 185, 62, 110),
        outline=(255, 211, 128, 210),
        width=2,
    )
    draw.text(
        (24, height - 42),
        "MIC16 SCIENTIFIC DEMO BACKGROUND · 非真实摄像头画面",
        font=_font(max(14, height // 40)),
        fill=(214, 231, 241, 220),
    )
    return np.asarray(image, dtype=np.uint8)


def _fit_rgb(rgb: np.ndarray, width: int, height: int) -> np.ndarray:
    array = np.asarray(rgb, dtype=np.uint8)
    if array.ndim != 3 or array.shape[2] != 3:
        raise ValueError(f"expected RGB [H,W,3], got {array.shape}")
    if array.shape[1] == width and array.shape[0] == height:
        return array.copy()
    if HAS_PIL:
        image = Image.fromarray(array, mode="RGB")
        image.thumbnail((width, height), Image.Resampling.LANCZOS)
        canvas = Image.new("RGB", (width, height), (12, 21, 30))
        offset = ((width - image.width) // 2, (height - image.height) // 2)
        canvas.paste(image, offset)
        return np.asarray(canvas, dtype=np.uint8)
    y_index = np.linspace(0, array.shape[0] - 1, height).astype(np.int64)
    x_index = np.linspace(0, array.shape[1] - 1, width).astype(np.int64)
    return array[y_index][:, x_index]


class DemoFrameProvider:
    fps_hint = 12.0

    def __init__(self, width: int, height: int) -> None:
        self.width = int(width)
        self.height = int(height)
        self.frame_index = 0

    def open(self) -> None:
        self.frame_index = 0

    def read(self) -> CameraFrame:
        frame = CameraFrame(
            rgb=make_demo_frame(self.width, self.height, frame_index=self.frame_index),
            source_kind="demo",
            source_label="科研演示背景（非真实摄像头）",
            frame_index=self.frame_index,
            captured_at=time.time(),
        )
        self.frame_index += 1
        return frame

    def close(self) -> None:
        return


class StaticImageProvider:
    fps_hint = 4.0

    def __init__(self, path: Path, width: int, height: int) -> None:
        self.path = Path(path)
        self.width = int(width)
        self.height = int(height)
        self.rgb: np.ndarray | None = None
        self.frame_index = 0

    def open(self) -> None:
        if not self.path.is_file():
            raise FileNotFoundError(f"静态图像不存在：{self.path}")
        if HAS_PIL:
            with Image.open(self.path) as image:
                rgb = np.asarray(image.convert("RGB"), dtype=np.uint8)
        else:
            cv2 = _import_cv2()
            bgr = cv2.imread(str(self.path), cv2.IMREAD_COLOR)
            if bgr is None:
                raise RuntimeError(f"无法读取静态图像：{self.path}")
            rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        self.rgb = _fit_rgb(rgb, self.width, self.height)
        self.frame_index = 0

    def read(self) -> CameraFrame:
        if self.rgb is None:
            raise RuntimeError("静态图像源尚未打开")
        frame = CameraFrame(
            rgb=self.rgb.copy(),
            source_kind="static",
            source_label=str(self.path),
            frame_index=self.frame_index,
            captured_at=time.time(),
        )
        self.frame_index += 1
        return frame

    def close(self) -> None:
        self.rgb = None


class OpenCvFrameProvider:
    def __init__(
        self,
        source_kind: str,
        source: int | Path,
        width: int,
        height: int,
    ) -> None:
        self.source_kind = source_kind
        self.source = source
        self.width = int(width)
        self.height = int(height)
        self.capture = None
        self.frame_index = 0
        self.fps_hint = 25.0

    def open(self) -> None:
        cv2 = _import_cv2()
        self.capture = cv2.VideoCapture(int(self.source) if self.source_kind == "usb" else str(self.source))
        if not self.capture.isOpened():
            raise RuntimeError(f"无法打开视频源：{self.source}")
        if self.source_kind == "usb":
            self.capture.set(cv2.CAP_PROP_FRAME_WIDTH, self.width)
            self.capture.set(cv2.CAP_PROP_FRAME_HEIGHT, self.height)
        fps = float(self.capture.get(cv2.CAP_PROP_FPS) or 0.0)
        if 1.0 <= fps <= 120.0:
            self.fps_hint = fps
        self.frame_index = 0

    def read(self) -> CameraFrame | None:
        if self.capture is None:
            raise RuntimeError("视频源尚未打开")
        cv2 = _import_cv2()
        ok, bgr = self.capture.read()
        if not ok and self.source_kind == "video":
            self.capture.set(cv2.CAP_PROP_POS_FRAMES, 0)
            ok, bgr = self.capture.read()
        if not ok or bgr is None:
            return None
        rgb = cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB)
        rgb = _fit_rgb(rgb, self.width, self.height)
        frame = CameraFrame(
            rgb=rgb,
            source_kind=self.source_kind,
            source_label=(f"USB camera {self.source}" if self.source_kind == "usb" else str(self.source)),
            frame_index=self.frame_index,
            captured_at=time.time(),
        )
        self.frame_index += 1
        return frame

    def close(self) -> None:
        if self.capture is not None:
            self.capture.release()
        self.capture = None


def create_frame_provider(
    source_kind: str,
    source_value: str = "",
    *,
    width: int = 1280,
    height: int = 720,
) -> FrameProvider:
    if source_kind == "科研演示背景":
        return DemoFrameProvider(width, height)
    if source_kind == "静态图像":
        return StaticImageProvider(Path(source_value), width, height)
    if source_kind == "视频文件":
        path = Path(source_value)
        if not path.is_file():
            raise FileNotFoundError(f"视频文件不存在：{path}")
        return OpenCvFrameProvider("video", path, width, height)
    if source_kind == "USB 摄像头":
        try:
            camera_index = int(source_value.strip() or "0")
        except ValueError as exc:
            raise ValueError("USB 摄像头编号必须是整数，例如 0") from exc
        return OpenCvFrameProvider("usb", camera_index, width, height)
    raise ValueError(f"不支持的视频源类型：{source_kind}")


class CameraWorker:
    """Read a FrameProvider in a worker thread and keep only recent frames."""

    def __init__(self, provider: FrameProvider) -> None:
        self.provider = provider
        self.events: queue.Queue[tuple[str, object]] = queue.Queue(maxsize=3)
        self.stop_event = threading.Event()
        self.thread: threading.Thread | None = None

    @property
    def running(self) -> bool:
        return self.thread is not None and self.thread.is_alive()

    def start(self) -> None:
        if self.running:
            return
        self.stop_event.clear()
        self.thread = threading.Thread(target=self._run, name="mic16-camera-worker", daemon=True)
        self.thread.start()

    def stop(self) -> None:
        self.stop_event.set()
        try:
            self.provider.close()
        except Exception:
            pass
        thread = self.thread
        if thread is not None and thread.is_alive() and thread is not threading.current_thread():
            thread.join(timeout=0.8)
        self.thread = None

    def _emit(self, kind: str, payload: object) -> None:
        try:
            self.events.put_nowait((kind, payload))
            return
        except queue.Full:
            pass
        try:
            self.events.get_nowait()
        except queue.Empty:
            pass
        try:
            self.events.put_nowait((kind, payload))
        except queue.Full:
            pass

    def _run(self) -> None:
        try:
            self.provider.open()
            self._emit("started", None)
            interval = 1.0 / max(1.0, min(60.0, float(self.provider.fps_hint)))
            while not self.stop_event.is_set():
                started = time.monotonic()
                frame = self.provider.read()
                if frame is None:
                    raise RuntimeError("视频源没有返回有效画面")
                self._emit("frame", frame)
                remaining = interval - (time.monotonic() - started)
                if remaining > 0:
                    self.stop_event.wait(remaining)
        except Exception as exc:  # noqa: BLE001 - worker reports errors to GUI
            self._emit("error", str(exc))
        finally:
            try:
                self.provider.close()
            except Exception:
                pass
            self._emit("stopped", None)

