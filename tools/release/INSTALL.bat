@echo off
setlocal
if not "%~1"=="" goto run
echo Harry Potter VR installer - your own US PC game is required.
echo Original PC files remain read-only. No game is launched automatically.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\INSTALL-HPVR.ps1" -PromptForGamePath
goto done
:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\INSTALL-HPVR.ps1" %*
:done
set "hpvr_exit=%errorlevel%"
echo.
if not "%hpvr_exit%"=="0" echo Installation stopped. Read the error above.
pause
exit /b %hpvr_exit%
