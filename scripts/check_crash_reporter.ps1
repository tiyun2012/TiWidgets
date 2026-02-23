param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

Write-Host "Building dx12_demo ($Config)..."
cmake --build $BuildDir --config $Config --target dx12_demo | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code $LASTEXITCODE"
}

$exe = Join-Path $repoRoot (Join-Path $BuildDir (Join-Path "bin\$Config" "dx12_demo.exe"))
if (!(Test-Path $exe)) {
    throw "Executable not found: $exe"
}

if (Test-Path "crash_report.log") {
    Remove-Item "crash_report.log" -Force
}
Get-ChildItem -Path $repoRoot -Filter "dx12_demo_crash_*.dmp" -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue

$env:DF_EVENT_CONSOLE = "0"
$env:DF_FORCE_CRASH = "1"

Write-Host "Running forced-crash check..."
& $exe
$exitCode = $LASTEXITCODE

Remove-Item Env:DF_EVENT_CONSOLE -ErrorAction SilentlyContinue
Remove-Item Env:DF_FORCE_CRASH -ErrorAction SilentlyContinue

if ($exitCode -eq 0) {
    throw "Expected nonzero exit from forced crash, got 0"
}

if (!(Test-Path "crash_report.log")) {
    throw "crash_report.log not found"
}

$dump = Get-ChildItem -Path $repoRoot -Filter "dx12_demo_crash_*.dmp" | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $dump) {
    throw "No crash dump file generated"
}

$lastCrash = Get-Content "crash_report.log" -Tail 1
Write-Host "Crash exit code: $exitCode"
Write-Host "Crash report: $lastCrash"
Write-Host "Crash dump: $($dump.Name)"
