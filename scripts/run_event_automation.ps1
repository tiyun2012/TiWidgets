param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [switch]$SkipCleanLog,
    [switch]$SkipCleanCrash,
    [switch]$ShowConsole,
    [switch]$VerboseEvents,
    [ValidateSet("baseline", "splitter_stress", "widget_drag_stress", "mixed", "resize_stress", "resize_crash_stress", "close_all", "global_float_sync", "host_transfer_stress")]
    [string]$Scenario = "baseline",
    [ValidateSet("dark", "light", "slate", "template")]
    [string]$Theme = "dark",
    [int]$MaxConflicts = -1,
    [int]$VisualizeDelayMs = 0,
    [int]$RenderFrames = 0,
    [switch]$ShowWindow,
    [switch]$ResizeDebug,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$env:DF_EVENT_CONSOLE = $(if ($ShowConsole) { "1" } else { "0" })
$env:DF_EVENT_VERBOSE = $(if ($VerboseEvents) { "1" } else { "0" })
$env:DF_AUTOMATE_EVENTS = "1"
$env:DF_AUTOMATION_SCENARIO = $Scenario
$env:DF_THEME = $Theme
$env:DF_RESIZE_DEBUG = $(if ($ResizeDebug -or $Scenario -like "resize*") { "1" } else { "0" })
$env:DF_AUTOMATION_EVENT_SLEEP_MS = $VisualizeDelayMs
$env:DF_AUTOMATION_RENDER_FRAMES = $RenderFrames
if ($ShowWindow) {
    $env:DF_EVENT_SHOW_WINDOW = "1"
} else {
    Remove-Item Env:DF_EVENT_SHOW_WINDOW -ErrorAction SilentlyContinue
}

if (-not $SkipCleanLog -and (Test-Path "event_conflicts.log")) {
    Remove-Item "event_conflicts.log" -Force -ErrorAction SilentlyContinue
}
if (-not $SkipCleanCrash -and (Test-Path "crash_report.log")) {
    Remove-Item "crash_report.log" -Force -ErrorAction SilentlyContinue
}
if (-not $SkipCleanCrash) {
    Get-ChildItem -Path $repoRoot -Filter "dx12_demo_crash_*.dmp" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
}

if (-not $SkipBuild) {
    Write-Host "Building dx12_demo ($Config)..."
    cmake --build $BuildDir --config $Config --target dx12_demo | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed with exit code $LASTEXITCODE"
    }
} else {
    Write-Host "Skipping build (using existing dx12_demo binary)."
}

$exe = Join-Path $repoRoot (Join-Path $BuildDir (Join-Path "bin\$Config" "dx12_demo.exe"))
if (!(Test-Path $exe)) {
    throw "Executable not found: $exe"
}

Write-Host "Running automated event checks (scenario=$Scenario theme=$Theme)..."
& $exe
$exitCode = $LASTEXITCODE

Write-Host "dx12_demo exit code: $exitCode"
if (Test-Path "event_conflicts.log") {
    $summary = Select-String -Path "event_conflicts.log" -Pattern '^\[auto\] summary' | Select-Object -Last 1
    $status = Select-String -Path "event_conflicts.log" -Pattern '^\[auto\] automation checks (passed|failed)' | Select-Object -Last 1
    if ($summary) { Write-Host $summary.Line }
    if ($status) { Write-Host $status.Line }
    if ($summary -and $MaxConflicts -ge 0) {
        $m = [regex]::Match($summary.Line, 'conflicts=(\d+)')
        if ($m.Success) {
            $conflicts = [int]$m.Groups[1].Value
            if ($conflicts -gt $MaxConflicts) {
                Write-Host "Conflict threshold exceeded: $conflicts > $MaxConflicts"
                $exitCode = 3
            }
        }
    }
    if ($VerboseEvents) {
        Write-Host "Last 40 log lines:"
        Get-Content "event_conflicts.log" -Tail 40 | Out-Host
    }
}

if ($exitCode -ne 0 -and (Test-Path "crash_report.log")) {
    Write-Host "Crash report tail:"
    Get-Content "crash_report.log" -Tail 20 | Out-Host
}
if ($exitCode -eq 0 -and (Test-Path "crash_report.log")) {
    $crashLines = Get-Content "crash_report.log" | Where-Object { $_ -match "exception=0x" }
    if ($crashLines.Count -gt 0) {
        Write-Host "Crash artifacts detected despite zero exit code."
        $crashLines | Select-Object -Last 5 | Out-Host
        $exitCode = 4
    }
}

Remove-Item Env:DF_AUTOMATION_SCENARIO -ErrorAction SilentlyContinue
Remove-Item Env:DF_EVENT_CONSOLE -ErrorAction SilentlyContinue
Remove-Item Env:DF_EVENT_VERBOSE -ErrorAction SilentlyContinue
Remove-Item Env:DF_AUTOMATE_EVENTS -ErrorAction SilentlyContinue
Remove-Item Env:DF_AUTOMATION_EVENT_SLEEP_MS -ErrorAction SilentlyContinue
Remove-Item Env:DF_AUTOMATION_RENDER_FRAMES -ErrorAction SilentlyContinue
Remove-Item Env:DF_THEME -ErrorAction SilentlyContinue
Remove-Item Env:DF_RESIZE_DEBUG -ErrorAction SilentlyContinue

exit $exitCode
