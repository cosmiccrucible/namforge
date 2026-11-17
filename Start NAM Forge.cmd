@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\start_app.ps1" %*
if errorlevel 1 (
  echo.
  echo NAM Forge launcher exited with an error.
  pause
)
