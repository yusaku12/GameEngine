[CmdletBinding()]
param(
    [string]$Flatc = $env:FLATBUFFERS_FLATC_EXECUTABLE
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$schema = Join-Path $root 'Schemas\FlatBuffers'
$output = Join-Path $root 'Generated\FlatBuffers'

if ([string]::IsNullOrWhiteSpace($Flatc)) {
    $repoFlatc = Join-Path $root 'Tools\FlatBuffers\flatc.exe'
    if (Test-Path $repoFlatc) {
        $Flatc = $repoFlatc
    } else {
        $command = Get-Command flatc.exe -ErrorAction SilentlyContinue
        if ($null -eq $command) {
            throw 'flatc.exe was not found. Set FLATBUFFERS_FLATC_EXECUTABLE or add flatc.exe to PATH.'
        }
        $Flatc = $command.Source
    }
}

if (-not (Test-Path $schema -PathType Container)) { throw "Schema directory was not found: $schema" }
New-Item -ItemType Directory -Force -Path $output | Out-Null

& $Flatc --cpp -o $output -I $schema (Join-Path $schema 'Common.fbs') (Join-Path $schema 'Model.fbs')
if ($LASTEXITCODE -ne 0) { throw "FlatBuffers schema generation failed with exit code $LASTEXITCODE." }
Write-Host 'FlatBuffers schema generation completed successfully.'