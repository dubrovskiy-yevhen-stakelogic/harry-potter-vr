@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\workspace\INSTALL-PLAYER.ps1" %*
set "install_result=%errorlevel%"
echo.
pause
exit /b %install_result%
