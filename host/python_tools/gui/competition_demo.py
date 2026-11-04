# -*- coding: utf-8 -*-
"""Competition-facing MIC16 visualization built from runtime evidence only."""

from __future__ import annotations

import math
import tkinter as tk
from collections import deque
from dataclasses import dataclass
from tkinter import ttk
from typing import Callable

from .theme import ACCENT, BAD, BG, GOOD, GRID, INK, MUTED, PANEL, PANEL_2, PLOT_BG, WARN


@dataclass(frozen=True)
class CompetitionDemoState:
    """Snapshot shown by the competition page.

    Empty values mean unavailable. They are intentionally rendered as pending,
    not replaced by simulated values.
    """

    source: str = "--"
    mode: str = "--"
    backend: str = "--"
    frame_id: int | None = None
    angle_deg: float | None = None
    top3_angles_deg: tuple[float, ...] = ()
    softmax_confidence: float | None = None
    confidence_head: float | None = None
    entropy: float | None = None
    frame_valid: bool = False
    quality_text: str = "等待定位结果"
    active_channels: int = 0
    active_pairs: int = 0
    packet_errors: int = 0
    latency_ms: float | None = None
    enhanced_frames: int = 0
    enhancement_source: str = "--"
    enhancement_age_s: float | None = None
    running: bool = False
    input_rms: float | None = None
    output_rms: float | None = None
    algorithm_version: str = "--"
    parameter_version: str = "--"


@dataclass(frozen=True)
class ProofCard:
    label: str
    value: str
    severity: str


def normalize_angle(angle_deg: float) -> float:
    return float(angle_deg) % 360.0


def evidence_label(source: str) -> tuple[str, str]:
    """Return a truthful origin label and its severity color key."""
    value = source.strip().upper()
    if "BOARD" in value or "板端" in source:
        return "板端实时接口", "good"
    if "REPLAY" in value or "回放" in source:
        return "录音离线回放", "warn"
    if "PC" in value:
        return "PC 实时参考", "accent"
    return "等待证据来源", "idle"


def active_pipeline_stages(state: CompetitionDemoState) -> int:
    """Return how many formal pipeline stages have runtime evidence."""
    if state.active_channels <= 0 and state.frame_id is None:
        return 0
    if state.angle_deg is None:
        return 1
    backend = state.backend.upper()
    if "GCC-PHAT/TDOA" in backend:
        return 2
    if backend == "SRP-PHAT" or " SRP-PHAT" in backend:
        return 3
    if "CA-QW-SRP-CCTCN" in backend or "RESULT_V2" in state.source.upper():
        return 6 if has_current_enhancement(state) else 5
    return 2


def has_current_enhancement(state: CompetitionDemoState) -> bool:
    """Only recent board audio can illuminate the board enhancement stage."""
    return (
        state.running
        and state.enhanced_frames > 0
        and evidence_label(state.source)[0] == "板端实时接口"
        and state.enhancement_source == "BOARD ENHANCED_AUDIO"
        and state.enhancement_age_s is not None
        and 0.0 <= state.enhancement_age_s <= 1.0
    )


def demo_headline(state: CompetitionDemoState) -> str:
    """Return one truthful sentence that a judge can understand immediately."""
    if state.active_channels <= 0 and state.frame_id is None:
        return "连接阵列或载入固定录音，开始四项证据演示"
    if not state.running:
        return "结果已冻结，仅供回看，不代表实时运行"
    if state.active_channels and state.active_channels < 16:
        if state.frame_valid and state.angle_deg is not None:
            return "部分通道健康检查未通过，当前定位门控判定有效"
        return "部分通道健康检查未通过，等待有效定位结果"
    if state.angle_deg is not None and not state.frame_valid:
        return "当前定位结果未通过质量门控，不应据此更新目标方向"
    if has_current_enhancement(state) and state.frame_valid:
        return "已接收板端增强音频，效果需通过同段 A/B 对比验证"
    if state.angle_deg is not None and state.frame_valid:
        return f"当前有效方位估计为 {normalize_angle(state.angle_deg):.1f}°"
    return "16通道数据已接收，等待有效定位结果"


def proof_cards(state: CompetitionDemoState) -> tuple[ProofCard, ...]:
    capture_severity = "good" if state.active_channels == 16 else (
        "warn" if state.active_channels > 0 else "idle"
    )
    if state.angle_deg is None:
        doa_value, doa_severity = "等待结果", "idle"
    elif state.frame_valid:
        doa_value, doa_severity = f"{normalize_angle(state.angle_deg):.1f}°", "good"
    else:
        doa_value, doa_severity = "质量拒识", "warn"
    pair_severity = "good" if state.active_pairs > 0 and state.frame_valid else (
        "warn" if state.active_pairs > 0 else "idle"
    )
    enhancement_severity = "good" if has_current_enhancement(state) else "idle"
    enhancement_value = "近期收到输出" if has_current_enhancement(state) else (
        f"历史接收 {state.enhanced_frames} 帧" if state.enhanced_frames else "等待真实输出"
    )
    return (
        ProofCard("通道健康", f"{state.active_channels}/16 通道", capture_severity),
        ProofCard("鲁棒定位", doa_value, doa_severity),
        ProofCard("可靠性门控", f"{state.active_pairs}/32 麦对" if state.active_pairs else "等待质量特征", pair_severity),
        ProofCard("空间增强", enhancement_value, enhancement_severity),
    )


class CompetitionDemoPanel(ttk.Frame):
    """Status-first competition page for live, replay, and board evidence."""

    PIPELINE_LABELS = (
        "16ch PCM",
        "GCC-PHAT",
        "CA-QW-SRP",
        "CCTCN",
        "质量门控",
        "DAS / MVDR",
    )

    def __init__(
        self,
        parent,
        *,
        on_capture: Callable[[], None],
        on_localize: Callable[[], None],
        on_replay: Callable[[], None],
        on_focus: Callable[[], None],
    ) -> None:
        super().__init__(parent, padding=4)
        self.state = CompetitionDemoState()
        self.angle_history: deque[float | None] = deque(maxlen=180)

        self.evidence_var = tk.StringVar(value="等待证据来源")
        self.headline_var = tk.StringVar(value=demo_headline(self.state))
        self.origin_detail_var = tk.StringVar(value="未收到 AUDIO 或 RESULT 数据")
        self.angle_var = tk.StringVar(value="--")
        self.confidence_var = tk.StringVar(value="--")
        self.quality_var = tk.StringVar(value="等待定位结果")
        self.top3_var = tk.StringVar(value="--")
        self.channel_var = tk.StringVar(value="0 / 16")
        self.pair_var = tk.StringVar(value="0 / 32")
        self.latency_var = tk.StringVar(value="--")
        self.packet_var = tk.StringVar(value="0")
        self.enhancement_var = tk.StringVar(value="等待增强输出")
        self.version_var = tk.StringVar(value="alg=--  param=--")

        self.grid_columnconfigure(0, weight=3)
        self.grid_columnconfigure(1, weight=2)
        self.grid_rowconfigure(1, weight=4)
        self.grid_rowconfigure(2, weight=2)

        self._build_toolbar(on_capture, on_localize, on_replay, on_focus)
        self._build_direction_panel()
        self._build_evidence_panel()
        self._build_timeline_panel()
        self._build_enhancement_panel()

    def _frame(self, parent, *, bg: str = PANEL) -> tk.Frame:
        return tk.Frame(parent, bg=bg, highlightbackground=GRID, highlightthickness=1, bd=0)

    def _build_toolbar(self, on_capture, on_localize, on_replay, on_focus) -> None:
        toolbar = self._frame(self)
        toolbar.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        toolbar.grid_columnconfigure(1, weight=1)
        tk.Label(
            toolbar,
            text="MIC16 空间语音感知演示",
            bg=PANEL,
            fg=INK,
            font=("Segoe UI", 15, "bold"),
        ).grid(row=0, column=0, sticky="w", padx=12, pady=(9, 0))
        self.evidence_label_widget = tk.Label(
            toolbar,
            textvariable=self.evidence_var,
            bg=PANEL,
            fg=MUTED,
            font=("Segoe UI", 11, "bold"),
        )
        self.evidence_label_widget.grid(row=0, column=1, sticky="e", padx=12, pady=(9, 0))
        tk.Label(
            toolbar,
            textvariable=self.headline_var,
            bg=PANEL,
            fg=INK,
            font=("Segoe UI", 15, "bold"),
        ).grid(row=1, column=0, columnspan=2, sticky="w", padx=12, pady=(2, 0))
        tk.Label(
            toolbar,
            textvariable=self.origin_detail_var,
            bg=PANEL,
            fg=MUTED,
            font=("Segoe UI", 9),
        ).grid(row=2, column=0, columnspan=2, sticky="w", padx=12, pady=(2, 7))

        actions = tk.Frame(toolbar, bg=PANEL)
        actions.grid(row=0, column=2, rowspan=3, sticky="e", padx=8, pady=7)
        ttk.Button(actions, text="连接采集", command=on_capture).pack(side=tk.LEFT, padx=3)
        ttk.Button(actions, text="PC 实时定位", command=on_localize).pack(side=tk.LEFT, padx=3)
        ttk.Button(actions, text="录音回放", command=on_replay).pack(side=tk.LEFT, padx=3)
        ttk.Button(actions, text="专注演示", command=on_focus).pack(side=tk.LEFT, padx=3)

        self.pipeline = tk.Canvas(toolbar, height=58, bg=PLOT_BG, highlightthickness=0)
        self.pipeline.grid(row=3, column=0, columnspan=3, sticky="ew", padx=10, pady=(0, 5))
        self.pipeline.bind("<Configure>", lambda _event: self._draw_pipeline())

        self.proof_canvas = tk.Canvas(toolbar, height=62, bg=PANEL, highlightthickness=0)
        self.proof_canvas.grid(row=4, column=0, columnspan=3, sticky="ew", padx=10, pady=(0, 9))
        self.proof_canvas.bind("<Configure>", lambda _event: self._draw_proofs())

    def _build_direction_panel(self) -> None:
        panel = self._frame(self, bg=PLOT_BG)
        panel.grid(row=1, column=0, sticky="nsew", padx=(0, 8))
        panel.grid_rowconfigure(0, weight=1)
        panel.grid_columnconfigure(0, weight=1)
        self.direction_canvas = tk.Canvas(panel, bg=PLOT_BG, highlightthickness=0)
        self.direction_canvas.grid(row=0, column=0, sticky="nsew")
        self.direction_canvas.bind("<Configure>", lambda _event: self._draw_direction())

    def _build_evidence_panel(self) -> None:
        panel = self._frame(self)
        panel.grid(row=1, column=1, sticky="nsew")
        panel.grid_columnconfigure(0, weight=1)
        tk.Label(panel, text="当前方位角", bg=PANEL, fg=MUTED, font=("Segoe UI", 10)).grid(
            row=0, column=0, sticky="w", padx=14, pady=(14, 0)
        )
        tk.Label(panel, textvariable=self.angle_var, bg=PANEL, fg=INK, font=("Consolas", 31, "bold")).grid(
            row=1, column=0, sticky="w", padx=14
        )
        self.quality_label_widget = tk.Label(
            panel,
            textvariable=self.quality_var,
            bg=PANEL,
            fg=MUTED,
            font=("Segoe UI", 13, "bold"),
        )
        self.quality_label_widget.grid(
            row=2, column=0, sticky="w", padx=14, pady=(1, 12)
        )

        rows = (
            ("置信度", self.confidence_var),
            ("Top-3", self.top3_var),
            ("健康通道", self.channel_var),
            ("有效麦对", self.pair_var),
            ("总延迟", self.latency_var),
            ("UDP 异常", self.packet_var),
            ("版本", self.version_var),
        )
        for row, (name, variable) in enumerate(rows, start=3):
            tk.Label(panel, text=name, bg=PANEL, fg=MUTED, font=("Segoe UI", 9)).grid(
                row=row, column=0, sticky="w", padx=14, pady=(6, 0)
            )
            tk.Label(panel, textvariable=variable, bg=PANEL, fg=INK, font=("Consolas", 11, "bold")).grid(
                row=row, column=1, sticky="e", padx=14, pady=(6, 0)
            )

        tk.Label(
            panel,
            text="提示：板端接口、PC 参考与录音回放分开标识；未验证指标不会自动填充。",
            bg=PANEL,
            fg=MUTED,
            font=("Segoe UI", 9),
            wraplength=390,
            justify=tk.LEFT,
        ).grid(row=10, column=0, columnspan=2, sticky="sw", padx=14, pady=14)
        panel.grid_rowconfigure(10, weight=1)

    def _build_timeline_panel(self) -> None:
        panel = self._frame(self)
        panel.grid(row=2, column=0, sticky="nsew", padx=(0, 8), pady=(8, 0))
        tk.Label(panel, text="连续方位轨迹（仅记录有效帧）", bg=PANEL, fg=MUTED, font=("Segoe UI", 10, "bold")).pack(
            anchor="w", padx=10, pady=(8, 0)
        )
        self.timeline_canvas = tk.Canvas(panel, height=150, bg=PLOT_BG, highlightthickness=0)
        self.timeline_canvas.pack(fill=tk.BOTH, expand=True, padx=8, pady=6)
        self.timeline_canvas.bind("<Configure>", lambda _event: self._draw_timeline())

    def _build_enhancement_panel(self) -> None:
        panel = self._frame(self)
        panel.grid(row=2, column=1, sticky="nsew", pady=(8, 0))
        tk.Label(panel, text="多通道增强输出", bg=PANEL, fg=MUTED, font=("Segoe UI", 10, "bold")).pack(
            anchor="w", padx=12, pady=(9, 2)
        )
        tk.Label(panel, textvariable=self.enhancement_var, bg=PANEL, fg=INK, font=("Segoe UI", 12, "bold")).pack(
            anchor="w", padx=12
        )
        self.level_canvas = tk.Canvas(panel, height=86, bg=PLOT_BG, highlightthickness=0)
        self.level_canvas.pack(fill=tk.X, padx=10, pady=(8, 5))
        self.level_canvas.bind("<Configure>", lambda _event: self._draw_levels())
        tk.Label(
            panel,
            text="电平仅用于运行诊断；没有干净参考时不显示 SI-SDR、STOI 或 PESQ。",
            bg=PANEL,
            fg=MUTED,
            font=("Segoe UI", 9),
            wraplength=410,
            justify=tk.LEFT,
        ).pack(anchor="w", padx=12, pady=(0, 8))

    def update_state(self, state: CompetitionDemoState) -> None:
        previous_frame = self.state.frame_id
        self.state = state
        if state.frame_id != previous_frame:
            self.angle_history.append(normalize_angle(state.angle_deg) if state.frame_valid and state.angle_deg is not None else None)

        label, color_key = evidence_label(state.source)
        colors = {"good": GOOD, "warn": WARN, "accent": ACCENT, "idle": MUTED}
        self.evidence_var.set(label)
        self.evidence_label_widget.configure(fg=colors[color_key])
        self.origin_detail_var.set(
            f"来源={state.source}  模式={state.mode}  后端={state.backend}  帧={state.frame_id if state.frame_id is not None else '--'}"
        )
        self.headline_var.set(demo_headline(state))
        self.angle_var.set("--" if state.angle_deg is None else f"{normalize_angle(state.angle_deg):06.2f}°")
        self.confidence_var.set(self._confidence_text(state))
        self.quality_var.set(state.quality_text or ("定位有效" if state.frame_valid else "当前帧无效"))
        self.top3_var.set(" / ".join(f"{normalize_angle(v):.0f}°" for v in state.top3_angles_deg) or "--")
        self.channel_var.set(f"{state.active_channels} / 16")
        self.pair_var.set(f"{state.active_pairs} / 32")
        self.latency_var.set("--" if state.latency_ms is None else f"{state.latency_ms:.2f} ms")
        self.packet_var.set(str(state.packet_errors))
        self.version_var.set(f"alg={state.algorithm_version}  param={state.parameter_version}")
        self.quality_label_widget.configure(
            fg=GOOD if state.frame_valid else (WARN if state.angle_deg is not None else MUTED)
        )
        self.enhancement_var.set(
            f"已接收 {state.enhanced_frames} 帧增强音频" if state.enhanced_frames else "等待真实增强音频输出"
        )
        self._draw_pipeline()
        self._draw_proofs()
        self._draw_direction()
        self._draw_timeline()
        self._draw_levels()

    @staticmethod
    def _confidence_text(state: CompetitionDemoState) -> str:
        parts: list[str] = []
        if state.softmax_confidence is not None:
            parts.append(f"softmax {state.softmax_confidence:.3f}")
        if state.confidence_head is not None:
            parts.append(f"head {state.confidence_head:.3f}")
        if state.entropy is not None:
            parts.append(f"H {state.entropy:.3f}")
        return " | ".join(parts) or "--"

    def _draw_pipeline(self) -> None:
        canvas = self.pipeline
        canvas.delete("all")
        width = max(720, int(canvas.winfo_width() or 720))
        active = active_pipeline_stages(self.state)
        margin = 12
        gap = 18
        box_w = (width - 2 * margin - gap * (len(self.PIPELINE_LABELS) - 1)) / len(self.PIPELINE_LABELS)
        for index, label in enumerate(self.PIPELINE_LABELS):
            x0 = margin + index * (box_w + gap)
            x1 = x0 + box_w
            color = GOOD if index < active else PANEL_2
            outline = GOOD if index < active else GRID
            canvas.create_rectangle(x0, 11, x1, 47, fill=color, outline=outline, width=1)
            canvas.create_text((x0 + x1) / 2, 29, text=label, fill="#FFFFFF" if index < active else MUTED, font=("Microsoft YaHei UI", 9, "bold"))
            if index + 1 < len(self.PIPELINE_LABELS):
                canvas.create_line(x1 + 3, 29, x1 + gap - 3, 29, fill=GRID, width=2, arrow=tk.LAST)

    def _draw_proofs(self) -> None:
        canvas = self.proof_canvas
        canvas.delete("all")
        width = max(760, int(canvas.winfo_width() or 760))
        cards = proof_cards(self.state)
        gap = 8
        card_w = (width - gap * (len(cards) - 1)) / len(cards)
        colors = {"good": GOOD, "warn": WARN, "bad": BAD, "idle": GRID}
        for index, card in enumerate(cards):
            x0 = index * (card_w + gap)
            x1 = x0 + card_w
            color = colors[card.severity]
            canvas.create_rectangle(x0, 3, x1, 57, fill=PLOT_BG, outline=color, width=2)
            canvas.create_oval(x0 + 11, 13, x0 + 21, 23, fill=color, outline=color)
            canvas.create_text(x0 + 29, 18, anchor="w", text=card.label, fill=MUTED, font=("Segoe UI", 9, "bold"))
            canvas.create_text(x0 + 11, 42, anchor="w", text=card.value, fill=INK, font=("Segoe UI", 11, "bold"))

    @staticmethod
    def _point(cx: float, cy: float, radius: float, angle_deg: float) -> tuple[float, float]:
        theta = math.radians(angle_deg)
        return cx + radius * math.cos(theta), cy - radius * math.sin(theta)

    def _draw_direction(self) -> None:
        c = self.direction_canvas
        c.delete("all")
        width = max(420, int(c.winfo_width() or 640))
        height = max(360, int(c.winfo_height() or 430))
        cx, cy = width * 0.5, height * 0.55
        radius = min(width * 0.36, height * 0.39)
        c.create_text(18, 16, anchor="nw", text="360° 声源方向与 16 麦圆阵", fill=MUTED, font=("Segoe UI", 11, "bold"))
        c.create_oval(cx - radius, cy - radius, cx + radius, cy + radius, outline=GRID, width=2)
        c.create_oval(cx - radius * 0.72, cy - radius * 0.72, cx + radius * 0.72, cy + radius * 0.72, outline=PANEL_2)

        for angle in range(0, 360, 30):
            x1, y1 = self._point(cx, cy, radius * 0.88, angle)
            x2, y2 = self._point(cx, cy, radius, angle)
            xt, yt = self._point(cx, cy, radius * 1.12, angle)
            c.create_line(x1, y1, x2, y2, fill=GRID)
            c.create_text(xt, yt, text=f"{angle}°", fill=MUTED, font=("Consolas", 9))

        for mic in range(16):
            angle = mic * 22.5
            x, y = self._point(cx, cy, radius * 0.72, angle)
            c.create_oval(x - 5, y - 5, x + 5, y + 5, fill=ACCENT, outline="#90CAF9")
            xt, yt = self._point(cx, cy, radius * 0.62, angle)
            c.create_text(xt, yt, text=str(mic), fill=MUTED, font=("Consolas", 7))

        for rank, angle in enumerate(self.state.top3_angles_deg[:3]):
            x, y = self._point(cx, cy, radius * (0.93 - rank * 0.08), angle)
            c.create_line(cx, cy, x, y, fill=(ACCENT, "#4E88C7", "#687784")[rank], width=(5, 3, 2)[rank])

        if self.state.angle_deg is not None:
            angle = normalize_angle(self.state.angle_deg)
            color = GOOD if self.state.frame_valid else WARN
            x, y = self._point(cx, cy, radius * 0.94, angle)
            c.create_line(cx, cy, x, y, fill=color, width=6, arrow=tk.LAST)
            c.create_oval(cx - 8, cy - 8, cx + 8, cy + 8, fill=color, outline=color)
            c.create_text(cx, cy + radius + 34, text=f"预测 {angle:.2f}°", fill=color, font=("Segoe UI", 16, "bold"))
        else:
            c.create_text(cx, cy, text="等待定位", fill=MUTED, font=("Segoe UI", 16, "bold"))

    def _draw_timeline(self) -> None:
        c = self.timeline_canvas
        c.delete("all")
        width = max(500, int(c.winfo_width() or 700))
        height = max(120, int(c.winfo_height() or 150))
        left, right, top, bottom = 44, width - 10, 12, height - 24
        for angle in (0, 90, 180, 270, 360):
            y = bottom - (angle / 360.0) * (bottom - top)
            c.create_line(left, y, right, y, fill=GRID)
            c.create_text(left - 6, y, anchor="e", text=str(angle), fill=MUTED, font=("Consolas", 8))
        values = list(self.angle_history)
        if len(values) < 2:
            c.create_text((left + right) / 2, (top + bottom) / 2, text="等待连续有效帧", fill=MUTED)
            return
        step = (right - left) / max(1, len(values) - 1)
        segment: list[float] = []
        for index, value in enumerate(values):
            if value is None:
                if len(segment) >= 4:
                    c.create_line(*segment, fill=GOOD, width=2)
                segment = []
                continue
            x = left + index * step
            y = bottom - (value / 360.0) * (bottom - top)
            segment.extend((x, y))
        if len(segment) >= 4:
            c.create_line(*segment, fill=GOOD, width=2)
        c.create_text(right, height - 8, anchor="e", text="最近 180 次结果更新", fill=MUTED, font=("Segoe UI", 8))

    def _draw_levels(self) -> None:
        c = self.level_canvas
        c.delete("all")
        width = max(360, int(c.winfo_width() or 400))
        values = (("16ch 输入 RMS", self.state.input_rms, ACCENT), ("增强输出 RMS", self.state.output_rms, GOOD))
        finite = [float(value) for _label, value, _color in values if value is not None and value >= 0.0]
        maximum = max(finite, default=1.0)
        for row, (label, value, color) in enumerate(values):
            y = 18 + row * 34
            c.create_text(8, y, anchor="w", text=label, fill=MUTED, font=("Segoe UI", 9))
            c.create_rectangle(116, y - 7, width - 74, y + 7, fill=PANEL_2, outline=GRID)
            if value is not None and value >= 0.0:
                fraction = min(1.0, float(value) / max(maximum, 1.0e-12))
                c.create_rectangle(116, y - 7, 116 + (width - 190) * fraction, y + 7, fill=color, outline=color)
                c.create_text(width - 8, y, anchor="e", text=f"{value:.1f} code", fill=INK, font=("Consolas", 9))
            else:
                c.create_text(width - 8, y, anchor="e", text="--", fill=MUTED, font=("Consolas", 9))
