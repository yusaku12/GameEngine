@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_schema.ps1" %*
exit /b %errorlevel%