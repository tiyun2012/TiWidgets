param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [string]$BuildDir = 'build_dx12',
    [ValidateSet('dark', 'light', 'slate', 'ocean', 'forest', 'rose', 'template')][string]$Theme,
    [string]$UiConfig = 'config/ui.ini',
    [string]$OutputPath = 'artifacts/visual/preview.png',
    [int]$Width = 1280,
    [int]$Height = 720,
    [switch]$BatchStress,
    [switch]$Profiler,
    [switch]$Diagnostics,
    [switch]$Gallery,
    [ValidateSet('Controls', 'Settings', 'ScrollList')][string]$GalleryPage = 'Controls',
    [switch]$NativeHosts,
    [switch]$LeaveOpen
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($BuildDir)) { $BuildDir = Join-Path $repoRoot $BuildDir }
$exe = Join-Path $BuildDir "bin/$Config/dx12_demo.exe"
if (-not (Test-Path -LiteralPath $exe)) { throw "Build dx12_demo first: $exe" }
if (-not [IO.Path]::IsPathRooted($UiConfig)) { $UiConfig = Join-Path $repoRoot $UiConfig }
if (-not (Test-Path -LiteralPath $UiConfig -PathType Leaf)) { throw "UI configuration does not exist: $UiConfig" }
$UiConfig = (Resolve-Path -LiteralPath $UiConfig).ProviderPath
if (-not [IO.Path]::IsPathRooted($OutputPath)) { $OutputPath = Join-Path $repoRoot $OutputPath }
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutputPath) | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class TiPreviewWindow {
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT point);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint message, IntPtr wParam, IntPtr lParam);
}
'@
[TiPreviewWindow]::SetProcessDPIAware() | Out-Null
$envNames = @('DF_AUTOMATE_EVENTS', 'DF_EVENT_CONSOLE', 'DF_EVENT_VERBOSE', 'DF_NATIVE_FLOAT_HOSTS', 'DF_THEME', 'DF_UI_CONFIG', 'DF_UI_GALLERY', 'DF_UI_GALLERY_PAGE', 'DF_UI_PROFILER', 'DF_CANVAS_BATCH_STRESS')
$savedEnv = @{}
foreach ($name in $envNames) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
$process = $null
try {
    $env:DF_AUTOMATE_EVENTS = '0'
    $env:DF_EVENT_CONSOLE = '0'
    $env:DF_EVENT_VERBOSE = '0'
    $env:DF_NATIVE_FLOAT_HOSTS = $(if ($NativeHosts) { '1' } else { '0' })
    # An omitted -Theme lets the file choose its preset, even if DF_THEME was inherited.
    [Environment]::SetEnvironmentVariable('DF_THEME', $(if ($PSBoundParameters.ContainsKey('Theme')) { $Theme } else { $null }), 'Process')
    $env:DF_UI_CONFIG = $UiConfig
    $env:DF_UI_GALLERY = $(if ($Gallery) { '1' } else { '0' })
    $env:DF_UI_GALLERY_PAGE = [string](@('Controls', 'Settings', 'ScrollList').IndexOf($GalleryPage))
    $env:DF_UI_PROFILER = $(if ($Profiler) { '1' } else { '0' })
    $env:DF_CANVAS_BATCH_STRESS = $(if ($BatchStress) { '1' } else { '0' })
    # A visible window is intentional: this command previews the interactive UI.
    $process = Start-Process -FilePath $exe -WorkingDirectory $repoRoot -PassThru -WindowStyle Normal
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
        if ($process.HasExited) { throw "Preview exited before rendering (exit $($process.ExitCode))." }
    } while ($process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline)
    if ($process.MainWindowHandle -eq 0) { throw 'Preview window did not appear.' }
    $handle = $process.MainWindowHandle
    $outer = New-Object TiPreviewWindow+RECT
    $client = New-Object TiPreviewWindow+RECT
    [TiPreviewWindow]::GetWindowRect($handle, [ref]$outer) | Out-Null
    [TiPreviewWindow]::GetClientRect($handle, [ref]$client) | Out-Null
    $frameW = ($outer.Right - $outer.Left) - $client.Right
    $frameH = ($outer.Bottom - $outer.Top) - $client.Bottom
    [TiPreviewWindow]::SetWindowPos($handle, [IntPtr]::Zero, 30, 30, ($Width + $frameW), ($Height + $frameH), 0x0040) | Out-Null
    [TiPreviewWindow]::SetForegroundWindow($handle) | Out-Null
    if ($Diagnostics) { [TiPreviewWindow]::SendMessage($handle, 0x0100, [IntPtr]0x70, [IntPtr]::Zero) | Out-Null }
    Start-Sleep -Milliseconds 900
    if (-not [TiPreviewWindow]::GetWindowRect($handle, [ref]$outer)) { throw 'Cannot read preview bounds.' }
    [TiPreviewWindow]::GetClientRect($handle, [ref]$client) | Out-Null
    $origin = New-Object TiPreviewWindow+POINT
    [TiPreviewWindow]::ClientToScreen($handle, [ref]$origin) | Out-Null
    $bitmap = New-Object Drawing.Bitmap $client.Right, $client.Bottom
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
        $bitmap.Save($OutputPath, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $graphics.Dispose(); $bitmap.Dispose() }
    Write-Host "Preview saved: $OutputPath"
} finally {
    if ($process -and -not $LeaveOpen -and -not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(3000)) { $process.Kill() }
    }
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
}
