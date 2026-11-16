@echo off
rem Patternflow exhibit laptop: power settings for the show. Double-click once.
rem
rem For the power plan in use, on the charger (AC) and on battery (DC), so that
rem pulling the charger out never stops the show:
rem   sleep after                               never       SUB_SLEEP      STANDBYIDLE
rem   hibernate after                           never       SUB_SLEEP      HIBERNATEIDLE
rem   closing the lid                           do nothing  SUB_BUTTONS    LIDACTION
rem   USB selective suspend                     disabled    SUB_USB        (USB selective suspend)
rem   PCI Express link state power management   off         SUB_PCIEXPRESS ASPM
rem then applies the plan (powercfg /setactive SCHEME_CURRENT) and prints the
rem new values. The display timeout is left as it is.
rem
rem By GUID, not alias: some aliases are missing on some machines (there is no
rem USBSELECTIVESUSPEND alias on the exhibit laptop). No administrator needed:
rem the power plan lets Users write it - powercfg /getsecuritydescriptor
rem SCHEME_CURRENT and ActionSetActive both show (A;CI;KRKW;;;BU) - and
rem /setacvalueindex, /setdcvalueindex and /setactive need no elevation.
rem A setting this computer does not have (no lid) is reported and skipped.
rem Safe to run again. The staff monitor (tools\monitor) checks the same values
rem every 60 s. ASCII only - cmd reads this file in the OEM codepage.
setlocal
set "SUB_SLEEP=238c9fa8-0aad-41ed-83f4-97be242c8f20"
set "STANDBYIDLE=29f6c1db-86da-48c5-9fdb-f2b67b1f44da"
set "HIBERNATEIDLE=9d7815a6-7ee4-497e-8888-515a05f02364"
set "SUB_BUTTONS=4f971e89-eebd-4455-a8de-9e59040e7347"
set "LIDACTION=5ca83367-6e45-459f-a27b-476b1d01c936"
set "SUB_USB=2a737441-1930-4402-8d77-b2bebba308a3"
set "USBSUSPEND=48e6b7a6-50f5-4782-a5d4-53bb8f07e226"
set "SUB_PCIEXPRESS=501a4d13-42af-4429-9fd1-a8218c268e20"
set "ASPM=ee12f906-d277-404b-b6da-e5fa1a576df5"
set /a FAILED=0

echo Patternflow exhibit laptop - power settings for the show
echo (for the power plan in use, on the charger and on battery)
echo.
call :apply "Sleep after: never" %SUB_SLEEP% %STANDBYIDLE%
call :apply "Hibernate after: never" %SUB_SLEEP% %HIBERNATEIDLE%
call :apply "Closing the lid: do nothing" %SUB_BUTTONS% %LIDACTION%
call :apply "USB selective suspend: disabled" %SUB_USB% %USBSUSPEND%
call :apply "PCI Express link state power management: off" %SUB_PCIEXPRESS% %ASPM%
echo Applying the power plan
powercfg /setactive SCHEME_CURRENT
if errorlevel 1 call :failed "applying the power plan" "powercfg /setactive SCHEME_CURRENT"

echo.
echo Now - both lines of each setting should read 0x00000000:
call :show "Sleep after, seconds" %SUB_SLEEP% %STANDBYIDLE%
call :show "Hibernate after, seconds" %SUB_SLEEP% %HIBERNATEIDLE%
call :show "Closing the lid, 0 = do nothing" %SUB_BUTTONS% %LIDACTION%
call :show "USB selective suspend, 0 = disabled" %SUB_USB% %USBSUSPEND%
call :show "PCI Express link state power management, 0 = off" %SUB_PCIEXPRESS% %ASPM%
echo.
if %FAILED% equ 0 goto all_done
echo %FAILED% change or changes did not work - see NOT CHANGED above.
echo - If powercfg said the setting does not exist, this computer does not
echo   have it, for example no lid. That is fine.
echo - If it said access is denied, close this window, right-click this file,
echo   choose "Run as administrator" and run it once more.
echo - Otherwise tell the operator what the window shows.
goto end

:all_done
echo Done. The staff monitor shows the new values within a minute.

:end
echo.
pause
endlocal
exit /b 0

rem ------------------------------------------------------------------ helpers
rem %1 name shown, %2 subgroup GUID, %3 setting GUID
:apply
echo %~1
powercfg /setacvalueindex SCHEME_CURRENT %2 %3 0
if errorlevel 1 call :failed "%~1" "on the charger - AC"
powercfg /setdcvalueindex SCHEME_CURRENT %2 %3 0
if errorlevel 1 call :failed "%~1" "on battery - DC"
exit /b 0

:failed
set /a FAILED+=1
echo   NOT CHANGED: %~1, %~2
exit /b 0

:show
echo %~1:
powercfg /qh SCHEME_CURRENT %2 %3 | findstr /c:" AC " /c:" DC "
if errorlevel 1 echo   not reported on this computer
exit /b 0
