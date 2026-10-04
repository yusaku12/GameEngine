<#
.SYNOPSIS
Converts a source image into a runtime DDS without modifying the source.
.DESCRIPTION
Color: BC7 sRGB; Data: BC7 Linear; Mask: BC4 Linear (red channel);
UI: RGBA8 sRGB, one mip by default; HDR: signed BC6H Linear.
Compressed outputs require dimensions divisible by four. No resizing is performed.
Output replacement is opt-in and happens only after successful conversion and validation.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Source,
    [Parameter(Mandatory = $true)]
    [string]$Output,
    [Parameter(Mandatory = $true)]
    [ValidateSet("Color", "Data", "Mask", "UI", "HDR")]
    [string]$Usage,
    [ValidateRange(0, 15)]
    [int]$MipLevels = 0,
    [ValidateRange(0.0, 1.0)]
    [double]$AlphaCutoff,
    [string]$Texconv = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$sourcePath = (Get-Item -LiteralPath $Source).FullName
if (!(Test-Path -LiteralPath $sourcePath -PathType Leaf)) { throw "Source is not a file: $Source" }
$extension = [IO.Path]::GetExtension($sourcePath).ToLowerInvariant()
if ($extension -notin @(".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".tga", ".hdr")) {
    throw "Unsupported source extension: $extension. Use the original raster image, not a cooked DDS."
}
if (($Usage -eq "HDR") -ne ($extension -eq ".hdr")) { throw "Use HDR only with .hdr sources; .hdr sources require HDR usage." }
if ($PSBoundParameters.ContainsKey("AlphaCutoff") -and $Usage -ne "Color") {
    throw "AlphaCutoff is supported only for Color usage."
}
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Output)
if ([IO.Path]::GetExtension($outputPath) -ine ".dds") { throw "Output must have a .dds extension." }
if ([string]::Equals($sourcePath, $outputPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Source and output must be different files."
}
if ((Test-Path -LiteralPath $outputPath) -and !$Force) { throw "Output already exists. Pass -Force to replace: $outputPath" }
if (Test-Path -LiteralPath $outputPath -PathType Container) { throw "Output is a directory: $outputPath" }

if ([string]::IsNullOrWhiteSpace($Texconv)) {
    $Texconv = Join-Path $projectRoot "Library\DirectXTex-main\Texconv\Bin\Desktop_2022_Win10\x64\Release\texconv.exe"
}
if (!(Test-Path -LiteralPath $Texconv -PathType Leaf)) {
    throw "texconv was not found: $Texconv. Run Tools\Textures\BuildTexconv.ps1 or pass -Texconv."
}
$Texconv = (Get-Item -LiteralPath $Texconv).FullName
$formats = @{ Color = "BC7_UNORM_SRGB"; Data = "BC7_UNORM"; Mask = "BC4_UNORM"; UI = "R8G8B8A8_UNORM_SRGB"; HDR = "BC6H_SF16" }
$formatIds = @{ Color = 99; Data = 98; Mask = 80; UI = 29; HDR = 96 }
if ($Usage -eq "UI" -and !$PSBoundParameters.ContainsKey("MipLevels")) { $MipLevels = 1 }
$outputDirectory = [IO.Path]::GetDirectoryName($outputPath)
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
$stagingDirectory = Join-Path $outputDirectory (".texture-cook-" + [Guid]::NewGuid().ToString("N"))
[IO.Directory]::CreateDirectory($stagingDirectory) | Out-Null
$stagedFile = Join-Path $stagingDirectory ([IO.Path]::GetFileNameWithoutExtension($sourcePath) + ".dds")

try {
    $arguments = @("-nologo", "-dx10", "-ft", "dds", "-f", $formats[$Usage], "-m", "$MipLevels",
        "--ignore-srgb", "-o", $stagingDirectory)
    if ($Usage -in @("Color", "UI")) { $arguments += @("-srgb", "-sepalpha") }
    if ($Usage -ne "UI") { $arguments += @("-nogpu", "-bc", "x") }
    if ($PSBoundParameters.ContainsKey("AlphaCutoff")) {
        $arguments += @("--keep-coverage", $AlphaCutoff.ToString([Globalization.CultureInfo]::InvariantCulture))
    }
    $arguments += $sourcePath
    Write-Host "[TextureCooker] Converting $sourcePath to $($formats[$Usage]); high-quality BC encoding can take several minutes."
    & $Texconv @arguments | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "texconv failed (exit code $LASTEXITCODE). Existing output was not changed." }
    if (!(Test-Path -LiteralPath $stagedFile -PathType Leaf)) { throw "texconv produced no DDS: $stagedFile" }

    $stream = [IO.File]::OpenRead($stagedFile)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        $header = $reader.ReadBytes(148)
        if ($header.Length -ne 148 -or [BitConverter]::ToUInt32($header, 0) -ne 0x20534444 -or
            [BitConverter]::ToUInt32($header, 4) -ne 124 -or
            [BitConverter]::ToUInt32($header, 76) -ne 32 -or
            [BitConverter]::ToUInt32($header, 84) -ne 0x30315844) { throw "Invalid DX10 DDS header." }
        $height = [BitConverter]::ToUInt32($header, 12)
        $width = [BitConverter]::ToUInt32($header, 16)
        $mips = [BitConverter]::ToUInt32($header, 28)
        $format = [BitConverter]::ToUInt32($header, 128)
        if ($format -ne $formatIds[$Usage] -or [BitConverter]::ToUInt32($header, 132) -ne 3 -or
            [BitConverter]::ToUInt32($header, 140) -ne 1 -or
            ([BitConverter]::ToUInt32($header, 136) -band 4) -ne 0) {
            throw "Unexpected DDS format or layout. Only a single Texture2D is supported by this converter."
        }
        if ($width -eq 0 -or $height -eq 0 -or $width -gt 16384 -or $height -gt 16384) {
            throw "DDS dimensions are outside the D3D12 Texture2D limits."
        }
        if ($Usage -ne "UI" -and (($width % 4) -ne 0 -or ($height % 4) -ne 0)) {
            throw "BC textures require width and height divisible by four. Resize the source explicitly or use UI for uncompressed color."
        }
        $fullMipCount = 1
        $dimension = [Math]::Max($width, $height)
        while ($dimension -gt 1) { $dimension = [Math]::Floor($dimension / 2); ++$fullMipCount }
        $expectedMips = $MipLevels
        if ($expectedMips -eq 0) { $expectedMips = $fullMipCount }
        if ($mips -ne $expectedMips -or $mips -gt $fullMipCount) { throw "Unexpected DDS mip count: $mips (expected $expectedMips)." }
        [long]$payloadBytes = 0
        [long]$mipWidth = $width
        [long]$mipHeight = $height
        for ($mip = 0; $mip -lt $mips; ++$mip) {
            if ($Usage -eq "UI") { $payloadBytes += $mipWidth * $mipHeight * 4 }
            else {
                $blockBytes = 16
                if ($Usage -eq "Mask") { $blockBytes = 8 }
                $payloadBytes += [long][Math]::Ceiling($mipWidth / 4.0) * [long][Math]::Ceiling($mipHeight / 4.0) * $blockBytes
            }
            $mipWidth = [long][Math]::Max(1, [Math]::Floor($mipWidth / 2.0))
            $mipHeight = [long][Math]::Max(1, [Math]::Floor($mipHeight / 2.0))
        }
        if ($stream.Length -ne 148 + $payloadBytes) { throw "DDS payload size does not match its format and mip chain." }
    }
    finally { $reader.Dispose() }

    if ($Force -and (Test-Path -LiteralPath $outputPath -PathType Leaf)) {
        [IO.File]::Replace($stagedFile, $outputPath, [NullString]::Value)
    }
    else { [IO.File]::Move($stagedFile, $outputPath) }
    Write-Host "[TextureCooker] $Usage -> $outputPath ($width x $height, $mips mips, $payloadBytes pixel bytes)"
}
finally {
    if (Test-Path -LiteralPath $stagedFile -PathType Leaf) { Remove-Item -LiteralPath $stagedFile -Force }
    Remove-Item -LiteralPath $stagingDirectory -Force
}
