@echo off
setlocal

set "ROOT=%~dp0..\.."
set "SCHEMA=%ROOT%\Schemas\FlatBuffers"
set "OUTPUT=%ROOT%\Generated\FlatBuffers"

if defined FLATBUFFERS_FLATC_EXECUTABLE (
    set "FLATC=%FLATBUFFERS_FLATC_EXECUTABLE%"
) else if exist "%ROOT%\Tools\FlatBuffers\flatc.exe" (
    set "FLATC=%ROOT%\Tools\FlatBuffers\flatc.exe"
) else (
    where flatc.exe >nul 2>nul
    if errorlevel 1 (
        echo ERROR: flatc.exe was not found. Set FLATBUFFERS_FLATC_EXECUTABLE or add flatc.exe to PATH.
        exit /b 1
    )
    set "FLATC=flatc.exe"
)

if not exist "%SCHEMA%" (
    echo ERROR: Schema directory was not found: "%SCHEMA%"
    exit /b 1
)

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

"%FLATC%" --cpp -o "%OUTPUT%" -I "%SCHEMA%" "%SCHEMA%\Common.fbs" "%SCHEMA%\Model.fbs"
if errorlevel 1 (
    echo ERROR: FlatBuffers schema generation failed.
    exit /b 1
)

echo FlatBuffers schema generation completed successfully.
endlocal