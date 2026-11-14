@echo off
rem Double-click: every panel the hub knows gets every pattern any of them has,
rem in one order (nothing is taken off a panel). The laptop joins the hub's
rem Wi-Fi for the length of it and goes back to its own Wi-Fi afterwards.
rem The pack it builds stays in pack-all\ beside the repository's README,
rem ready for:  panels.py join <firmware.bin> --pack pack-all
chcp 65001 >nul
cd /d "%~dp0..\.."
python ensemble\tools\panels.py --hub-wifi patterns-push --union -y
echo.
pause
