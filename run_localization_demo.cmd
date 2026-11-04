@echo off
setlocal
chcp 65001 >nul
if not defined MIC16_PYTHON set "MIC16_PYTHON=python"
"%MIC16_PYTHON%" -B -X utf8 "%~dp0host\python_tools\mic16_qt_gui.py" --localization-demo
if errorlevel 1 pause
