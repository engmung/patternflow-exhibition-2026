@echo off
rem Patternflow exhibit monitor: starts pf_monitor.py in the background (pythonw,
rem no console window) and opens the dashboard http://127.0.0.1:8765/ in an Edge
rem app window (no tabs, no address bar), else in the default browser.
rem Running it again is safe: a second monitor sees the first and exits, and
rem only the window opens. ASCII only - cmd reads this file in the OEM codepage.
setlocal
cd /d "%~dp0"
set "URL=http://127.0.0.1:8765/"

set "PYW="
for %%P in (pythonw.exe) do set "PYW=%%~$PATH:P"
if defined PYW goto run_pythonw
where pyw >nul 2>nul
if not errorlevel 1 goto run_pyw
rem no pythonw: a minimised console window instead
start "PF monitor" /min python "%~dp0pf_monitor.py"
goto wait_init

:run_pythonw
start "" "%PYW%" "%~dp0pf_monitor.py"
goto wait_init

:run_pyw
start "" pyw -3 "%~dp0pf_monitor.py"

:wait_init
rem wait (up to ~20 s) until the monitor answers, so the window does not open on an error page
set /a TRIES=0
where curl >nul 2>nul
if errorlevel 1 goto no_curl
:wait
curl -s -o nul -m 1 "%URL%api/ping"
if not errorlevel 1 goto open
set /a TRIES+=1
if %TRIES% geq 20 goto open
ping -n 2 127.0.0.1 >nul
goto wait
:no_curl
ping -n 4 127.0.0.1 >nul

:open
set "EDGE=%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"
if exist "%EDGE%" goto edge
set "EDGE=%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"
if exist "%EDGE%" goto edge
set "EDGE=%LOCALAPPDATA%\Microsoft\Edge\Application\msedge.exe"
if exist "%EDGE%" goto edge
start "" "%URL%"
goto done

:edge
start "" "%EDGE%" --app=%URL% --window-size=1280,860

:done
endlocal
