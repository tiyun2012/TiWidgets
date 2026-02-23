param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [int]$Iterations = 5,
    [switch]$ShowWindow,
    [int]$VisualizeDelayMs = 0,
    [int]$RenderFrames = 0
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ($Iterations -lt 1) { $Iterations = 1 }

Write-Host "Building dx12_demo ($Config)..."
cmake --build $BuildDir --config $Config --target dx12_demo | Out-Host
if ($LASTEXITCODE -ne 0) {
    throw "Build failed (exit $LASTEXITCODE)"
}

$artifactRoot = Join-Path $repoRoot "artifacts\transfer_debug"
New-Item -ItemType Directory -Path $artifactRoot -Force | Out-Null
$runStamp = Get-Date -Format "yyyyMMdd_HHmmss"
$runDir = Join-Path $artifactRoot ("run_" + $runStamp)
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$scenarios = @("host_transfer_stress", "global_float_sync", "baseline")
$results = @()

for ($i = 1; $i -le $Iterations; $i++) {
    foreach ($scenario in $scenarios) {
        Write-Host ("[{0}/{1}] scenario={2}" -f $i, $Iterations, $scenario)
        $args = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\scripts\run_event_automation.ps1",
            "-Config", $Config,
            "-BuildDir", $BuildDir,
            "-Scenario", $scenario,
            "-SkipBuild",
            "-VisualizeDelayMs", $VisualizeDelayMs,
            "-RenderFrames", $RenderFrames
        )
        if ($ShowWindow) { $args += "-ShowWindow" }

        powershell @args | Out-Host
        $exitCode = $LASTEXITCODE
        $results += [PSCustomObject]@{
            Iteration = $i
            Scenario = $scenario
            ExitCode = $exitCode
            Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
        }

        if (Test-Path "event_conflicts.log") {
            Copy-Item "event_conflicts.log" (Join-Path $runDir ("event_{0}_{1}.log" -f $i, $scenario)) -Force
        }
        if (Test-Path "crash_report.log") {
            Copy-Item "crash_report.log" (Join-Path $runDir ("crash_{0}_{1}.log" -f $i, $scenario)) -Force
        }

        if ($exitCode -ne 0) {
            Write-Host ("Failure detected at iteration={0} scenario={1}" -f $i, $scenario)
            $results | ConvertTo-Json -Depth 4 | Out-File (Join-Path $runDir "summary.json") -Encoding UTF8
            exit $exitCode
        }
    }
}

$results | ConvertTo-Json -Depth 4 | Out-File (Join-Path $runDir "summary.json") -Encoding UTF8
Write-Host ("Transfer/debug checks passed. Artifacts: {0}" -f $runDir)
