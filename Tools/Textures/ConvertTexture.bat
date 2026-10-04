@echo off
setlocal DisableDelayedExpansion
if "%~1"=="" goto interactive
if "%~2"=="" goto dropped
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ConvertTexture.ps1" %*
exit /b %ERRORLEVEL%

:dropped
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ConvertTextureInteractive.ps1" -Source "%~1"
set "result=%ERRORLEVEL%"
pause
exit /b %result%

:interactive
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0ConvertTextureInteractive.ps1"
set "result=%ERRORLEVEL%"
pause
exit /b %result%
