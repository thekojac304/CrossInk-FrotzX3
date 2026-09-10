@echo off
setlocal DisableDelayedExpansion
title FrotzX3 Installer
"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\FrotzX3-Installer-Wizard.ps1"
echo.
echo Press any key to close this window.
pause >nul
endlocal
