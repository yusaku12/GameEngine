@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0BuildTexconv.ps1" %*
set "result=%ERRORLEVEL%"
if "%~1"=="" pause
exit /b %result%
