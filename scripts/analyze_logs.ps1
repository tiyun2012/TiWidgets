param(
    [string]$EventLog = "event_conflicts.log",
    [string]$CrashLog = "crash_report.log",
    [int]$MaxConflicts = 30,
    [int]$MaxUnhandled = 0
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if (!(Test-Path $EventLog)) {
    throw "Event log not found: $EventLog"
}

$summary = Select-String -Path $EventLog -Pattern '^\[auto\] summary' | Select-Object -Last 1
$status = Select-String -Path $EventLog -Pattern '^\[auto\] automation checks (passed|failed)' | Select-Object -Last 1
if (-not $summary) {
    throw "No automation summary found in $EventLog"
}
if (-not $status) {
    throw "No automation status found in $EventLog"
}

Write-Host $summary.Line
Write-Host $status.Line

$conflictMatch = [regex]::Match($summary.Line, 'conflicts=(\d+)')
$noneMatch = [regex]::Match($summary.Line, 'none=(\d+)')
$conflicts = $(if ($conflictMatch.Success) { [int]$conflictMatch.Groups[1].Value } else { -1 })
$noneCount = $(if ($noneMatch.Success) { [int]$noneMatch.Groups[1].Value } else { -1 })

if ($status.Line -match 'failed') {
    throw "Automation status is failed."
}
if ($conflicts -lt 0 -or $noneCount -lt 0) {
    throw "Could not parse conflicts/none counts from summary."
}
if ($conflicts -gt $MaxConflicts) {
    throw "Conflicts exceed threshold: $conflicts > $MaxConflicts"
}
if ($noneCount -gt $MaxUnhandled) {
    throw "Unhandled events exceed threshold: $noneCount > $MaxUnhandled"
}

if (Test-Path $CrashLog) {
    $crashes = Get-Content $CrashLog | Measure-Object | Select-Object -ExpandProperty Count
    Write-Host "Crash log entries: $crashes"
}

Write-Host "Log analysis passed."
