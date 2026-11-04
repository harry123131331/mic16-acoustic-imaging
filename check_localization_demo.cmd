@echo off
setlocal
chcp 65001 >nul
if not defined MIC16_PYTHON set "MIC16_PYTHON=python"
"%MIC16_PYTHON%" -B -X utf8 "%~dp0board\runtime.py" --check
if errorlevel 1 pause
