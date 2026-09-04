@echo off
setlocal
if exist "%~dp0..\..\Assets\Shaders\Compiled" rmdir /s /q "%~dp0..\..\Assets\Shaders\Compiled"
if exist "%~dp0..\..\Assets\Shaders\.cache" rmdir /s /q "%~dp0..\..\Assets\Shaders\.cache"
exit /b 0