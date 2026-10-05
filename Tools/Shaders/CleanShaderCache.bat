@echo off
setlocal
if exist "%~dp0..\..\Assets\Shaders\Compiled" (
    rmdir /s /q "%~dp0..\..\Assets\Shaders\Compiled"
    if exist "%~dp0..\..\Assets\Shaders\Compiled" exit /b 1
)
if exist "%~dp0..\..\Assets\Shaders\.cache" (
    rmdir /s /q "%~dp0..\..\Assets\Shaders\.cache"
    if exist "%~dp0..\..\Assets\Shaders\.cache" exit /b 1
)
exit /b 0