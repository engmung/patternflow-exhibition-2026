@echo off
rem One-time setup: "Patternflow monitor" shortcuts in the Startup folder and on
rem the Desktop (see install_startup.ps1). Add -Remove to take them away.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0install_startup.ps1" %*
pause
