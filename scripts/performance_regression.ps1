param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [string[]]$Scenarios = @("baseline", "mixed", "resize_stress", "close_all"),
    [int]$Iterations = 3,
    [int]$RenderFrames = 90,
    [int]$ThresholdPct = 20,
    [string]$BaselinePath = "",
    [string]$OutputPath = "",
    [switch]$UpdateBaseline,
    [switch]$FailOnMissingBaseline
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

if ($Iterations -lt 1) { throw "Iterations must be >= 1" }
if ($ThresholdPct -lt 0) { throw "ThresholdPct must be >= 0" }

$perfDir = Join-Path $repoRoot "artifacts\perf"
New-Item -ItemType Directory -Path $perfDir -Force | Out-Null

if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $perfDir ("baseline_{0}.json" -f $Config.ToLowerInvariant())
}
if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $OutputPath = Join-Path $perfDir ("report_{0}.json" -f (Get-Date -Format "yyyyMMdd_HHmmss"))
}

function Get-Average {
    param([double[]]$Values)
    if (-not $Values -or $Values.Count -eq 0) { return 0.0 }
    return ($Values | Measure-Object -Average).Average
}

function Parse-PerfLog {
    param([string]$LogPath)
    if (-not (Test-Path $LogPath)) {
        throw "Missing log file: $LogPath"
    }

    $eventLine = Select-String -Path $LogPath -Pattern '^\[auto\] perf_events' | Select-Object -Last 1
    $frameLine = Select-String -Path $LogPath -Pattern '^\[auto\] perf_frames' | Select-Object -Last 1
    if (-not $eventLine) {
        throw "Missing perf_events line in $LogPath"
    }
    if (-not $frameLine) {
        throw "Missing perf_frames line in $LogPath. Set RenderFrames > 0."
    }

    $mEvent = [regex]::Match($eventLine.Line, 'count=(\d+)\s+avg_ms=([0-9.]+)\s+p95_ms=([0-9.]+)')
    $mFrame = [regex]::Match($frameLine.Line, 'count=(\d+)\s+avg_ms=([0-9.]+)\s+p95_ms=([0-9.]+)\s+avg_fps=([0-9.]+)')
    if (-not $mEvent.Success) {
        throw "Could not parse perf_events metrics: $($eventLine.Line)"
    }
    if (-not $mFrame.Success) {
        throw "Could not parse perf_frames metrics: $($frameLine.Line)"
    }

    return [PSCustomObject]@{
        EventCount = [int]$mEvent.Groups[1].Value
        EventAvgMs = [double]$mEvent.Groups[2].Value
        EventP95Ms = [double]$mEvent.Groups[3].Value
        FrameCount = [int]$mFrame.Groups[1].Value
        FrameAvgMs = [double]$mFrame.Groups[2].Value
        FrameP95Ms = [double]$mFrame.Groups[3].Value
        AvgFps = [double]$mFrame.Groups[4].Value
    }
}

function Run-ScenarioPerf {
    param([string]$Scenario)

    $runDurations = New-Object System.Collections.Generic.List[double]
    $eventCount = New-Object System.Collections.Generic.List[double]
    $eventAvg = New-Object System.Collections.Generic.List[double]
    $eventP95 = New-Object System.Collections.Generic.List[double]
    $frameCount = New-Object System.Collections.Generic.List[double]
    $frameAvg = New-Object System.Collections.Generic.List[double]
    $frameP95 = New-Object System.Collections.Generic.List[double]
    $fpsAvg = New-Object System.Collections.Generic.List[double]

    for ($i = 1; $i -le $Iterations; $i++) {
        Write-Host "Perf run scenario=$Scenario iteration=$i/$Iterations"
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" `
            -Config $Config `
            -BuildDir $BuildDir `
            -Scenario $Scenario `
            -SkipBuild `
            -RenderFrames $RenderFrames | Out-Host
        $sw.Stop()
        if ($LASTEXITCODE -ne 0) {
            throw "run_event_automation failed for scenario=$Scenario iteration=$i (exit $LASTEXITCODE)"
        }

        $metrics = Parse-PerfLog -LogPath "event_conflicts.log"
        $runDurations.Add($sw.Elapsed.TotalMilliseconds)
        $eventCount.Add($metrics.EventCount)
        $eventAvg.Add($metrics.EventAvgMs)
        $eventP95.Add($metrics.EventP95Ms)
        $frameCount.Add($metrics.FrameCount)
        $frameAvg.Add($metrics.FrameAvgMs)
        $frameP95.Add($metrics.FrameP95Ms)
        $fpsAvg.Add($metrics.AvgFps)
    }

    return [ordered]@{
        Iterations = $Iterations
        RenderFrames = $RenderFrames
        RunMsAvg = [math]::Round((Get-Average -Values $runDurations.ToArray()), 3)
        RunMsPerFrame = [math]::Round(((Get-Average -Values $runDurations.ToArray()) / [math]::Max(1, $RenderFrames)), 4)
        EventCountAvg = [math]::Round((Get-Average -Values $eventCount.ToArray()), 1)
        EventAvgMs = [math]::Round((Get-Average -Values $eventAvg.ToArray()), 3)
        EventP95Ms = [math]::Round((Get-Average -Values $eventP95.ToArray()), 3)
        FrameCountAvg = [math]::Round((Get-Average -Values $frameCount.ToArray()), 1)
        FrameAvgMs = [math]::Round((Get-Average -Values $frameAvg.ToArray()), 3)
        FrameP95Ms = [math]::Round((Get-Average -Values $frameP95.ToArray()), 3)
        AvgFps = [math]::Round((Get-Average -Values $fpsAvg.ToArray()), 2)
    }
}

$scenarioResults = [ordered]@{}
foreach ($scenario in $Scenarios) {
    $scenarioResults[$scenario] = Run-ScenarioPerf -Scenario $scenario
}

$report = [ordered]@{
    Timestamp = (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
    Config = $Config
    ThresholdPct = $ThresholdPct
    Scenarios = $scenarioResults
    BaselinePath = $BaselinePath
    Status = "Passed"
    Regressions = @()
}

if (-not (Test-Path $BaselinePath) -or $UpdateBaseline) {
    $baseline = [ordered]@{
        Timestamp = (Get-Date -Format "yyyy-MM-dd HH:mm:ss")
        Config = $Config
        Scenarios = $scenarioResults
    }
    $baseline | ConvertTo-Json -Depth 8 | Out-File $BaselinePath -Encoding UTF8
    if ($UpdateBaseline) {
        Write-Host "Performance baseline updated: $BaselinePath"
    } else {
        Write-Host "Performance baseline created: $BaselinePath"
    }
    $report | ConvertTo-Json -Depth 8 | Out-File $OutputPath -Encoding UTF8
    Write-Host "Performance report: $OutputPath"
    if ($FailOnMissingBaseline -and -not $UpdateBaseline) {
        throw "Baseline was missing and created. Re-run to enforce regression checks."
    }
    exit 0
}

$baselineData = Get-Content $BaselinePath -Raw | ConvertFrom-Json
$regressions = New-Object System.Collections.Generic.List[object]

function Add-Regressions {
    param(
        [string]$Scenario,
        [string]$Metric,
        [double]$BaselineValue,
        [double]$CurrentValue,
        [bool]$HigherIsWorse
    )
    if ($BaselineValue -le 0) { return }
    if ($HigherIsWorse) {
        $limit = $BaselineValue * (1.0 + ($ThresholdPct / 100.0))
        if ($CurrentValue -gt $limit) {
            $regressions.Add([ordered]@{
                Scenario = $Scenario
                Metric = $Metric
                Baseline = [math]::Round($BaselineValue, 3)
                Current = [math]::Round($CurrentValue, 3)
                Threshold = [math]::Round($limit, 3)
            }) | Out-Null
        }
    } else {
        $floor = $BaselineValue * (1.0 - ($ThresholdPct / 100.0))
        if ($CurrentValue -lt $floor) {
            $regressions.Add([ordered]@{
                Scenario = $Scenario
                Metric = $Metric
                Baseline = [math]::Round($BaselineValue, 3)
                Current = [math]::Round($CurrentValue, 3)
                Threshold = [math]::Round($floor, 3)
            }) | Out-Null
        }
    }
}

function Get-BaselineRunMsPerFrame {
    param($BaselineScenario)
    if ($null -ne $BaselineScenario.RunMsPerFrame) {
        return [double]$BaselineScenario.RunMsPerFrame
    }
    $baseFrames = [math]::Max(1, [int]$BaselineScenario.RenderFrames)
    if ($null -ne $BaselineScenario.RunMsAvg) {
        return [double]$BaselineScenario.RunMsAvg / $baseFrames
    }
    return 0.0
}

foreach ($scenario in $Scenarios) {
    if (-not $scenarioResults.Contains($scenario)) { continue }
    $current = $scenarioResults[$scenario]
    $baselineScenario = $baselineData.Scenarios.$scenario
    if (-not $baselineScenario) { continue }

    $baselineRunMsPerFrame = Get-BaselineRunMsPerFrame -BaselineScenario $baselineScenario
    Add-Regressions -Scenario $scenario -Metric "RunMsPerFrame" -BaselineValue $baselineRunMsPerFrame -CurrentValue ([double]$current.RunMsPerFrame) -HigherIsWorse $true
    $baselineEventCount = if ($null -ne $baselineScenario.EventCountAvg) { [double]$baselineScenario.EventCountAvg } else { 0.0 }
    $currentEventCount = if ($null -ne $current.EventCountAvg) { [double]$current.EventCountAvg } else { 0.0 }
    $shouldCheckEventLatency = ($baselineEventCount -ge 10.0 -and $currentEventCount -ge 10.0)
    if ($shouldCheckEventLatency) {
        Add-Regressions -Scenario $scenario -Metric "EventAvgMs" -BaselineValue ([double]$baselineScenario.EventAvgMs) -CurrentValue ([double]$current.EventAvgMs) -HigherIsWorse $true
        Add-Regressions -Scenario $scenario -Metric "EventP95Ms" -BaselineValue ([double]$baselineScenario.EventP95Ms) -CurrentValue ([double]$current.EventP95Ms) -HigherIsWorse $true
    }
    Add-Regressions -Scenario $scenario -Metric "FrameAvgMs" -BaselineValue ([double]$baselineScenario.FrameAvgMs) -CurrentValue ([double]$current.FrameAvgMs) -HigherIsWorse $true
    Add-Regressions -Scenario $scenario -Metric "FrameP95Ms" -BaselineValue ([double]$baselineScenario.FrameP95Ms) -CurrentValue ([double]$current.FrameP95Ms) -HigherIsWorse $true
    Add-Regressions -Scenario $scenario -Metric "AvgFps" -BaselineValue ([double]$baselineScenario.AvgFps) -CurrentValue ([double]$current.AvgFps) -HigherIsWorse $false
}

if ($regressions.Count -gt 0) {
    $report.Status = "Failed"
    $report.Regressions = $regressions
    Write-Host "Performance regressions detected:" -ForegroundColor Red
    $regressions | ForEach-Object {
        Write-Host (" - scenario={0} metric={1} baseline={2} current={3} threshold={4}" -f $_.Scenario, $_.Metric, $_.Baseline, $_.Current, $_.Threshold)
    }
} else {
    Write-Host "Performance regression check passed."
}

$report | ConvertTo-Json -Depth 8 | Out-File $OutputPath -Encoding UTF8
Write-Host "Performance report: $OutputPath"

if ($report.Status -ne "Passed") {
    exit 1
}
