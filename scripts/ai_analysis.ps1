param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [switch]$RunEdgeCases,
    [switch]$RunResourceCheck,
    [switch]$RunCodeAnalysis,
    [switch]$RunPerformanceCheck,
    [switch]$UpdatePerfBaseline,
    [int]$PerfIterations = 3,
    [int]$PerfThresholdPct = 20,
    [int]$PerfRenderFrames = 90
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$report = [ordered]@{
    Timestamp = (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
    Config = $Config
    Results = @()
    Status = "Passed"
}

function Invoke-Step {
    param([string]$Name, [scriptblock]$Action)
    try {
        & $Action
        $report.Results += [ordered]@{ Name = $Name; Status = "Passed" }
    } catch {
        $report.Results += [ordered]@{ Name = $Name; Status = "Failed"; Error = $_.Exception.Message }
        $script:report.Status = "Failed"
    }
}

Invoke-Step -Name "EventAutomation" -Action {
    powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" -Config $Config -BuildDir $BuildDir | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "run_event_automation exit $LASTEXITCODE" }
}

Invoke-Step -Name "HostTransferStress" -Action {
    powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" -Config $Config -BuildDir $BuildDir -Scenario host_transfer_stress | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "host_transfer_stress exit $LASTEXITCODE" }
}

Invoke-Step -Name "LogAnalysis" -Action {
    powershell -ExecutionPolicy Bypass -File ".\scripts\analyze_logs.ps1" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "analyze_logs exit $LASTEXITCODE" }
}

if ($RunEdgeCases) {
    Invoke-Step -Name "EdgeCases" -Action {
        powershell -ExecutionPolicy Bypass -File ".\scripts\test_edge_cases.ps1" -Config $Config -BuildDir $BuildDir | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "test_edge_cases exit $LASTEXITCODE" }
    }
}

if ($RunResourceCheck) {
    Invoke-Step -Name "ResourceCheck" -Action {
        powershell -ExecutionPolicy Bypass -File ".\scripts\check_resources.ps1" -Config $Config -BuildDir $BuildDir | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "check_resources exit $LASTEXITCODE" }
    }
}

if ($RunCodeAnalysis) {
    Invoke-Step -Name "CodeAnalysis" -Action {
        powershell -ExecutionPolicy Bypass -File ".\scripts\analyze_code.ps1" | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "analyze_code exit $LASTEXITCODE" }
    }
}

if ($RunPerformanceCheck) {
    Invoke-Step -Name "PerformanceRegression" -Action {
        $perfArgs = @(
            "-ExecutionPolicy", "Bypass",
            "-File", ".\scripts\performance_regression.ps1",
            "-Config", $Config,
            "-BuildDir", $BuildDir,
            "-Iterations", $PerfIterations,
            "-ThresholdPct", $PerfThresholdPct,
            "-RenderFrames", $PerfRenderFrames
        )
        if ($UpdatePerfBaseline) { $perfArgs += "-UpdateBaseline" }
        powershell @perfArgs | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "performance_regression exit $LASTEXITCODE" }
    }
}

$jsonName = "analysis_report_{0}.json" -f (Get-Date -Format "yyyyMMdd_HHmmss")
$report | ConvertTo-Json -Depth 6 | Out-File $jsonName -Encoding UTF8
Write-Host "Analysis report: $jsonName"
Write-Host "Overall status: $($report.Status)"
if ($report.Status -ne "Passed") {
    exit 1
}
