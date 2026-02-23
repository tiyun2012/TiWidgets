param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [ValidateSet("dark", "light", "slate", "template")]
    [string]$Theme = "dark",
    [string]$Baseline = "",
    [int]$Tolerance = 1800,
    [switch]$UpdateBaseline,
    [int]$RenderFrames = 90,
    [int]$WindowWaitMs = 5000
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;

public static class User32Native {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
"@

function Get-BaselinePath {
    param([string]$Cfg, [string]$ThemeName)
    $dir = Join-Path $repoRoot "artifacts\visual"
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    return Join-Path $dir ("baseline_{0}_{1}.png" -f $Cfg.ToLowerInvariant(), $ThemeName.ToLowerInvariant())
}

function Capture-Window {
    param(
        [IntPtr]$Handle,
        [string]$Path
    )

    $rect = New-Object User32Native+RECT
    if (-not [User32Native]::GetWindowRect($Handle, [ref]$rect)) {
        throw "GetWindowRect failed for window handle $Handle"
    }

    $width = [Math]::Max(1, $rect.Right - $rect.Left)
    $height = [Math]::Max(1, $rect.Bottom - $rect.Top)
    $bmp = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($bmp)
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
    $graphics.Dispose()
    $bmp.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
}

function Compare-Images {
    param(
        [string]$A,
        [string]$B,
        [int]$Step = 3
    )

    $imgA = [System.Drawing.Bitmap]::FromFile($A)
    $imgB = [System.Drawing.Bitmap]::FromFile($B)
    if ($imgA.Width -ne $imgB.Width -or $imgA.Height -ne $imgB.Height) {
        $wA = $imgA.Width
        $hA = $imgA.Height
        $wB = $imgB.Width
        $hB = $imgB.Height
        $imgA.Dispose()
        $imgB.Dispose()
        throw "Image dimensions differ: ${wA}x${hA} vs ${wB}x${hB}"
    }

    $diff = 0
    for ($x = 0; $x -lt $imgA.Width; $x += $Step) {
        for ($y = 0; $y -lt $imgA.Height; $y += $Step) {
            $pa = $imgA.GetPixel($x, $y)
            $pb = $imgB.GetPixel($x, $y)
            if ($pa.R -ne $pb.R -or $pa.G -ne $pb.G -or $pa.B -ne $pb.B) {
                $diff++
            }
        }
    }
    $imgA.Dispose()
    $imgB.Dispose()
    return $diff
}

$env:DF_AUTOMATE_EVENTS = "1"
$env:DF_AUTOMATION_SCENARIO = "baseline"
$env:DF_AUTOMATION_RENDER_FRAMES = $RenderFrames
$env:DF_EVENT_SHOW_WINDOW = "1"
$env:DF_EVENT_CONSOLE = "0"
$env:DF_EVENT_VERBOSE = "0"
$env:DF_THEME = $Theme

$exe = Join-Path $repoRoot (Join-Path $BuildDir (Join-Path "bin\$Config" "dx12_demo.exe"))
if (-not (Test-Path $exe)) {
    throw "Executable not found: $exe (build first)"
}

if ([string]::IsNullOrWhiteSpace($Baseline)) {
    $Baseline = Get-BaselinePath -Cfg $Config -ThemeName $Theme
}

Write-Host "Running dx12_demo for visual capture..."
$process = Start-Process -FilePath $exe -PassThru -WindowStyle Normal

try {
    $waitUntil = [DateTime]::UtcNow.AddMilliseconds($WindowWaitMs)
    while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $waitUntil -and -not $process.HasExited) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }

    if ($process.MainWindowHandle -eq 0) {
        throw "Could not locate dx12_demo window handle for capture."
    }

    Start-Sleep -Milliseconds 400
    $capturePath = Join-Path $repoRoot "artifacts\visual\capture.png"
    New-Item -ItemType Directory -Force -Path (Split-Path $capturePath) | Out-Null
    Capture-Window -Handle $process.MainWindowHandle -Path $capturePath

    if ($UpdateBaseline -or -not (Test-Path $Baseline)) {
        Copy-Item $capturePath -Destination $Baseline -Force
        Write-Host "Baseline updated at $Baseline"
        exit 0
    }

    $diff = Compare-Images -A $Baseline -B $capturePath
    Write-Host "Visual diff count: $diff (tolerance $Tolerance)"
    if ($diff -gt $Tolerance) {
        Write-Host "Visual regression detected"
        exit 1
    }

    Write-Host "Visual validation passed"
    exit 0
}
finally {
    try {
        if (-not $process.HasExited) {
            $process.Kill()
        }
    } catch {}
}
