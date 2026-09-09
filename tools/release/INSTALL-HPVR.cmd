@echo off
setlocal
if not "%~1"=="" goto run
echo Harry Potter VR demo installer - your own US PC game is required.
echo This does not launch or uninstall any game. Original PC files remain read-only.
echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0INSTALL-HPVR.ps1" -PromptForGamePath
goto done
:run
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0INSTALL-HPVR.ps1" %*
:done
set "hpvr_exit=%errorlevel%"
echo.
if not "%hpvr_exit%"=="0" echo Installation stopped. Read the error above; no game is launched automatically.
pause
exit /b %hpvr_exit%
