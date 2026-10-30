@echo off
rem Patternflow exhibit staff monitor - double-click to open it.
rem Starts the monitor in the background if it is not running yet, then opens
rem its window (tools\monitor\start_monitor.bat does the work; README there).
rem ASCII only - cmd reads this file in the OEM codepage.
call "%~dp0tools\monitor\start_monitor.bat"
