param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Debug",
    [switch] $AddressSanitizer
)

$ErrorActionPreference = "Stop"
$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$vswherePath = Join-Path ([Environment]::GetFolderPath([Environment+SpecialFolder]::ProgramFilesX86)) "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw "Visual Studio Installer's vswhere.exe was not found at '$vswherePath'."
}
$visualStudioPath = (& $vswherePath -latest -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $visualStudioPath) {
    throw "A Visual Studio C++ installation was not found."
}
$environmentScript = Join-Path $visualStudioPath "VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path -LiteralPath $environmentScript)) {
    throw "The x64 compiler environment was not found at '$environmentScript'."
}

$flags = if ($Configuration -eq "Debug") { "/MTd /D_DEBUG" } else { "/MT /DNDEBUG" }
$testConfiguration = $Configuration
if ($AddressSanitizer) {
    $flags += " /fsanitize=address /Zi"
    $testConfiguration += "Asan"
    $versionFile = Join-Path $visualStudioPath "VC\Auxiliary\Build\Microsoft.VCToolsVersion.default.txt"
    $compilerVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    $compilerDirectory = Join-Path $visualStudioPath "VC\Tools\MSVC\$compilerVersion\bin\Hostx64\x64"
    $env:PATH = $compilerDirectory + ";" + $env:PATH
}
$buildDirectory = Join-Path $repositoryRoot "Build\Tests\CoreRegression\$testConfiguration"
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
$executable = Join-Path $buildDirectory "CoreRegression.exe"
$sourceFiles = @(
    "Core\Threading\JobSystem.cpp",
    "Core\Threading\ThreadUtility.cpp",
    "Core\Threading\ThreadDebugStats.cpp",
    "Core\Threading\MainThreadDispatcher.cpp",
    "Core\Time\TimeManager.cpp",
    "Core\Time\HighResolutionTimer.cpp",
    "Core\Containers\String.cpp",
    "Core\Logging\Logger.cpp",
    "Core\Logging\Assert.cpp"
)
$sources = $sourceFiles | ForEach-Object { '"' + (Join-Path $repositoryRoot "Source\$_") + '"' }
$includes = @("Source", "Library", "Library\spdlog\include", "Library\DirectXTex-main\Common") |
    ForEach-Object { '/I"' + (Join-Path $repositoryRoot $_) + '"' }
$command = 'call "' + $environmentScript + '" >nul && cd /d "' + $buildDirectory +
    '" && cl /nologo /std:c++20 /EHsc /W4 /WX /utf-8 ' + $flags + ' ' + ($includes -join ' ') +
    ' "' + (Join-Path $PSScriptRoot "CoreRegression.cpp") + '" ' + ($sources -join ' ') +
    ' /Fe:"' + $executable + '"'
& $env:ComSpec /c $command
if ($LASTEXITCODE -ne 0) {
    throw "Core regression compilation failed for $testConfiguration."
}

foreach ($case in @("dependency", "nested", "group", "allocation")) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $executable
    $startInfo.Arguments = $case
    $startInfo.WorkingDirectory = $buildDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $startInfo
    $started = $false
    try {
        $started = $process.Start()
        if (-not $started) {
            throw "Could not start the '$case' regression."
        }
        $stdout = $process.StandardOutput.ReadToEndAsync()
        $stderr = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(20000)) {
            throw "Core regression '$case' timed out after 20 seconds."
        }
        $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
        $logPath = Join-Path $buildDirectory "$case.log"
        [System.IO.File]::WriteAllText($logPath, $output)
        if ($process.ExitCode -ne 0) {
            throw "Core regression '$case' failed (exit $($process.ExitCode)):`n$output"
        }
        $summary = ($output -split "`r?`n" | Where-Object { $_ -match '^\d+ core checks passed$' })
        if (-not $summary) {
            throw "Core regression '$case' produced no completion summary:`n$output"
        }
        Write-Output "${testConfiguration} ${case}: $summary"
    }
    finally {
        if ($started -and -not $process.HasExited) {
            Stop-Process -Id $process.Id
            $process.WaitForExit()
        }
        $process.Dispose()
    }
}
