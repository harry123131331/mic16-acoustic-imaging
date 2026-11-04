# -*- coding: utf-8 -*-
"""统一解析当前提交工程的源码、录音、结果与可选MATLAB路径。"""

from __future__ import annotations

import os
import shutil
from pathlib import Path


MATLAB_EXE_HINTS = (
    Path(r"D:\matlab\bin\matlab.exe"),
    Path(r"C:\Program Files\MATLAB\R2024a\bin\matlab.exe"),
)


def active_project_dir(start: Path | None = None) -> Path:
    """寻找当前python_tools所属目录，不回退到个人旧工程。"""
    here = Path(start or __file__).resolve()
    for parent in (here, *here.parents):
        if (parent / "python_tools").is_dir() and (
            (parent / "matlab_tools").is_dir() or parent.name == "python_class"
            or (parent / "release/pc_incremental_c16_frozen_20261015").is_dir()
        ):
            return parent
    return here.parent


def workspace_root(start: Path | None = None) -> Path:
    """结果与文档归当前提交根目录，不跨写旧研究工程。"""
    project = active_project_dir(start)
    if project.name == "host" and (project.parent / "firmware").is_dir():
        return project.parent
    if project.name == "mic16_next_version":
        return project.parent
    for parent in (project, *project.parents):
        if (parent / "mic16_next_version").is_dir():
            return parent
    return project.parent


def formal_project_dir(start: Path | None = None) -> Path:
    project = workspace_root(start) / "mic16_next_version"
    return project if project.is_dir() else active_project_dir(start)


def recording_root(start: Path | None = None) -> Path:
    """录音归当前GUI数据目录，不混入共享结果图。"""
    return active_project_dir(start) / "data" / "recordings"


def matlab_capture_reports_root(start: Path | None = None) -> Path:
    return workspace_root(start) / "results" / "matlab" / "capture_reports"


def matlab_tools_dir(start: Path | None = None) -> Path:
    path = formal_project_dir(start) / "matlab_tools"
    if not path.is_dir():
        raise FileNotFoundError(f"MATLAB tools directory not found: {path}")
    return path


def find_matlab_executable(explicit: str = "") -> Path:
    candidates: list[Path] = []
    if explicit.strip():
        candidates.append(Path(explicit).expanduser())
    env_value = os.environ.get("MIC16_MATLAB_EXE", "").strip()
    if env_value:
        candidates.append(Path(env_value).expanduser())
    candidates.extend(MATLAB_EXE_HINTS)
    on_path = shutil.which("matlab")
    if on_path:
        candidates.append(Path(on_path))
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise FileNotFoundError(
        "MATLAB executable not found. Set MIC16_MATLAB_EXE or select matlab.exe in the GUI."
    )
