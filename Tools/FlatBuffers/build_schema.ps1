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
if (-not (Test-Path -LiteralPath $Flatc -PathType Leaf)) { throw "flatc.exe was not found: $Flatc" }
$schemas = @(Get-ChildItem -LiteralPath $schema -Filter '*.fbs' -File | Sort-Object Name)
if ($schemas.Count -eq 0) { throw "No FlatBuffers schemas were found: $schema" }
New-Item -ItemType Directory -Force -Path $output | Out-Null

& $Flatc --cpp -o $output -I $schema $schemas.FullName
if ($LASTEXITCODE -ne 0) { throw "FlatBuffers schema generation failed with exit code $LASTEXITCODE." }
$expectedHeaders = @($schemas | ForEach-Object { "$($_.BaseName)_generated.h" })
foreach ($header in $expectedHeaders) {
    if (-not (Test-Path -LiteralPath (Join-Path $output $header) -PathType Leaf)) {
        throw "FlatBuffers generation produced no header: $header"
    }
}
Get-ChildItem -LiteralPath $output -Filter '*_generated.h' -File |
    Where-Object { $_.Name -notin $expectedHeaders } |
    ForEach-Object { Remove-Item -LiteralPath $_.FullName -Force }
Write-Host 'FlatBuffers schema generation completed successfully.'