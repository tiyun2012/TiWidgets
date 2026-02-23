param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$scenarios = @(
    @{ Name = "baseline"; MaxConflicts = 8 },
    @{ Name = "widget_drag_stress"; MaxConflicts = 8 },
    @{ Name = "splitter_stress"; MaxConflicts = 40 },
    @{ Name = "mixed"; MaxConflicts = 30 },
    @{ Name = "resize_stress"; MaxConflicts = 60 },
    @{ Name = "resize_crash_stress"; MaxConflicts = 100 },
    @{ Name = "global_float_sync"; MaxConflicts = 10 },
    @{ Name = "host_transfer_stress"; MaxConflicts = 20 },
    @{ Name = "close_all"; MaxConflicts = 10 }
)

foreach ($scenario in $scenarios) {
    Write-Host "Running scenario: $($scenario.Name)"
    powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" `
        -Config $Config `
        -BuildDir $BuildDir `
        -Scenario $scenario.Name `
        -MaxConflicts $scenario.MaxConflicts | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Scenario failed: $($scenario.Name) (exit $LASTEXITCODE)"
    }
}

Write-Host "Edge-case scenarios passed."
