[CmdletBinding()]
param([string]$Source = "")

$ErrorActionPreference = "Stop"

function Read-Path([string]$Prompt) {
    $value = (Read-Host $Prompt).Trim()
    if ($value.Length -ge 2 -and $value.StartsWith('"') -and $value.EndsWith('"')) {
        $value = $value.Substring(1, $value.Length - 2)
    }
    return $value
}

Write-Host "DDS Texture Converter"
Write-Host "The source image is kept unchanged. Enter Q at the source prompt to cancel."
if ([string]::IsNullOrWhiteSpace($Source)) {
    $Source = Read-Path "Source image path"
    if ($Source -eq "Q") { exit 0 }
}
if ([string]::IsNullOrWhiteSpace($Source) -or !(Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Source image was not found: $Source"
}
$Source = (Get-Item -LiteralPath $Source).FullName
Write-Host "1: Color (BC7 sRGB: albedo, base color, emissive)"
Write-Host "2: Data  (BC7 Linear: numerical data, XYZ normals)"
Write-Host "3: Mask  (BC4 Linear: red channel only)"
Write-Host "4: UI    (RGBA8 sRGB: uncompressed, exact pixels)"
Write-Host "5: HDR   (BC6H: .hdr source only)"
$choices = @{ "1" = "Color"; "2" = "Data"; "3" = "Mask"; "4" = "UI"; "5" = "HDR" }
$selection = (Read-Host "Usage number (required)").Trim()
if (!$choices.ContainsKey($selection)) { throw "Select a usage number from 1 to 5." }
$usage = $choices[$selection]
$defaultOutput = [IO.Path]::ChangeExtension($Source, ".dds")
$Output = Read-Path "Output DDS path (Enter: $defaultOutput)"
if ([string]::IsNullOrWhiteSpace($Output)) { $Output = $defaultOutput }
$Output = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
$force = $false
if (Test-Path -LiteralPath $Output -PathType Leaf) {
    $answer = (Read-Host "Output exists. Replace it? [y/N]").Trim()
    if ($answer -ine "y") {
        Write-Host "Cancelled. Existing output was not changed."
        exit 0
    }
    $force = $true
}
Write-Host "Source: $Source"
Write-Host "Output: $Output"
Write-Host "Usage:  $usage"
& (Join-Path $PSScriptRoot "ConvertTexture.ps1") -Source $Source -Output $Output -Usage $usage -Force:$force
Write-Host "DDS conversion completed. Material references are not changed automatically."
