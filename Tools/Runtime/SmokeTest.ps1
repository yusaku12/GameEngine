[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$executable = Join-Path $root "x64\$Configuration\GameEngine.exe"
$fixture = Join-Path $root "x64\$Configuration\RuntimeTests\RuntimeFixture.scene"
if (!(Test-Path -LiteralPath $executable) -or !(Test-Path -LiteralPath $fixture)) {
    throw "Build GameEngine and run RunTests.ps1 first."
}

Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public static class RuntimeWindowTest
{
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr window, out Rect rect);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr window, int x, int y, int width, int height, bool repaint);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr window, uint message, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll")] public static extern IntPtr GetWindow(IntPtr window, uint command);

    public static int CountTopLevelWindows(IntPtr first, uint processId)
    {
        int count = 0;
        for (IntPtr window = first; window != IntPtr.Zero; window = GetWindow(window, 2))
        {
            uint owner;
            GetWindowThreadProcessId(window, out owner);
            if (owner == processId && GetParent(window) == IntPtr.Zero) ++count;
        }
        return count;
    }

    public static bool HasRuntimeColor(IntPtr window)
    {
        Rect rect;
        if (!GetClientRect(window, out rect)) throw new InvalidOperationException("GetClientRect failed.");
        using (var bitmap = new Bitmap(rect.Right, rect.Bottom))
        using (var graphics = Graphics.FromImage(bitmap))
        {
            IntPtr dc = graphics.GetHdc();
            bool captured;
            try { captured = PrintWindow(window, dc, 3); }
            finally { graphics.ReleaseHdc(dc); }
            if (!captured) throw new InvalidOperationException("PrintWindow failed.");
            for (int y = 1; y <= 3; ++y)
                for (int x = 1; x <= 3; ++x)
                {
                    Color pixel = bitmap.GetPixel(rect.Right * x / 4, rect.Bottom * y / 4);
                    if (Math.Abs(pixel.R - 179) > 2 || Math.Abs(pixel.G - 26) > 2 || Math.Abs(pixel.B - 51) > 2)
                        return false;
                }
            return true;
        }
    }
}
'@

function Start-Engine([string]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $executable
    $info.WorkingDirectory = $root
    $info.Arguments = $Arguments
    $info.UseShellExecute = $false
    [System.Diagnostics.Process]::Start($info)
}

function Assert-Exit([string]$Arguments, [bool]$Success) {
    $process = Start-Engine $Arguments
    try {
        if (!$process.WaitForExit(15000)) { throw "Startup did not terminate: $Arguments" }
        if (($process.ExitCode -eq 0) -ne $Success) { throw "Unexpected startup exit: $Arguments" }
    } finally {
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
}

Assert-Exit "--help" $true
Assert-Exit "--runtime" $false
Assert-Exit "--unknown" $false
Assert-Exit "--runtime --scene `"NoSuchRuntimeScene.scene`"" $false

$unicodeFixture = Join-Path (Split-Path $fixture) ("Runtime " + [char]0x30b7 + [char]0x30fc + [char]0x30f3 + [Guid]::NewGuid().ToString("N") + ".scene")
Copy-Item -LiteralPath $fixture -Destination $unicodeFixture
$process = $null
try {
    $process = Start-Engine "--runtime --scene `"$unicodeFixture`""
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        if ($process.HasExited) { throw "Runtime terminated before readiness: $($process.ExitCode)" }
        $process.Refresh()
        $window = $process.MainWindowHandle
        if ($window -ne [IntPtr]::Zero -and $process.MainWindowTitle.Contains("FPS")) { break }
        Start-Sleep -Milliseconds 100
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero -or !$process.MainWindowTitle.Contains("FPS")) {
        throw "Runtime did not become responsive."
    }
    $first = [RuntimeWindowTest]::GetWindow($window, 0)
    if ([RuntimeWindowTest]::CountTopLevelWindows($first, $process.Id) -ne 1) {
        throw "Runtime created an unexpected extra window."
    }
    if (![RuntimeWindowTest]::HasRuntimeColor($window)) { throw "Runtime did not present the saved camera color." }
    if (![RuntimeWindowTest]::MoveWindow($window, 100, 100, 820, 500, $true)) { throw "Runtime window resize failed." }
    Start-Sleep -Milliseconds 500
    if (![RuntimeWindowTest]::HasRuntimeColor($window)) { throw "Runtime resize lost its rendered image." }
    if (![RuntimeWindowTest]::PostMessage($window, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)) {
        throw "Runtime close failed."
    }
    if (!$process.WaitForExit(15000) -or $process.ExitCode -ne 0) { throw "Runtime shutdown failed." }
    Write-Host "PASS: standalone CLI, Unicode/space path, single Runtime window, saved camera pixels, resize, clean shutdown."
} finally {
    if ($null -ne $process) {
        if (!$process.HasExited) { Stop-Process -Id $process.Id }
        $process.Dispose()
    }
    Remove-Item -LiteralPath $unicodeFixture
}
