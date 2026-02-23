param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [switch]$Reconfigure,
    [switch]$SkipCtest,
    [switch]$CheckCrashReporter,
    [switch]$RunEdgeCases,
    [switch]$CheckResources,
    [switch]$AnalyzeCode,
    [switch]$RunAnalysis,
    [switch]$Nightly,
    [string]$NightlyRoot = "artifacts",
    [int]$NightlyKeep = 14,
    [switch]$VisualValidation,
    [switch]$RunPerformance,
    [switch]$CheckIntelliSense,
    [switch]$UpdatePerfBaseline,
    [int]$PerfIterations = 3,
    [int]$PerfThresholdPct = 20,
    [int]$PerfRenderFrames = 90
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$resolvedBuildDir = $BuildDir

$doReconfigure = $Reconfigure.IsPresent
$doSkipCtest = $SkipCtest.IsPresent
$doCheckCrashReporter = $CheckCrashReporter.IsPresent
$doRunEdgeCases = $RunEdgeCases.IsPresent
$doCheckResources = $CheckResources.IsPresent
$doAnalyzeCode = $AnalyzeCode.IsPresent
$doRunAnalysis = $RunAnalysis.IsPresent
$doVisualValidation = $VisualValidation.IsPresent
$doRunPerformance = $RunPerformance.IsPresent
$doCheckIntelliSense = $CheckIntelliSense.IsPresent

if ($Nightly) {
    # Nightly mode runs the full quality sweep by default.
    $doReconfigure = $true
    $doSkipCtest = $false
    $doCheckCrashReporter = $true
    $doRunEdgeCases = $true
    $doCheckResources = $true
    $doAnalyzeCode = $true
    $doRunAnalysis = $true
    $doVisualValidation = $true
    $doRunPerformance = $true
    $doCheckIntelliSense = $true
}

$stepResults = @()
$runStatus = "Passed"
$runError = ""
$nightlyDir = $null
$transcriptStarted = $false
$runStart = Get-Date

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host "[$Name]"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        & $Action
        $sw.Stop()
        $script:stepResults += [PSCustomObject]@{
            Step = $Name
            Status = "Passed"
            DurationMs = [int]$sw.ElapsedMilliseconds
        }
    } catch {
        $sw.Stop()
        $script:stepResults += [PSCustomObject]@{
            Step = $Name
            Status = "Failed"
            DurationMs = [int]$sw.ElapsedMilliseconds
            Error = $_.Exception.Message
        }
        throw
    }
}

function Assert-LastExit {
    param([string]$Message)
    if ($LASTEXITCODE -ne 0) {
        throw "$Message (exit $LASTEXITCODE)"
    }
}

function Reset-CMakeConfigureState {
    param([string]$BuildDirPath)

    $cachePath = Join-Path $BuildDirPath "CMakeCache.txt"
    $filesPath = Join-Path $BuildDirPath "CMakeFiles"

    if (Test-Path $cachePath) {
        try {
            Remove-Item $cachePath -Force -ErrorAction Stop
        } catch {
            Write-Host "Warning: could not remove $cachePath ($($_.Exception.Message)). Continuing."
        }
    }
    if (Test-Path $filesPath) {
        try {
            Remove-Item $filesPath -Recurse -Force -ErrorAction Stop
        } catch {
            Write-Host "Warning: could not fully remove $filesPath ($($_.Exception.Message)). Continuing."
        }
    }
}

try {
    if ($Nightly) {
        $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
        $nightlyDir = Join-Path $repoRoot (Join-Path $NightlyRoot ("nightly_" + $stamp))
        New-Item -ItemType Directory -Path $nightlyDir -Force | Out-Null
        try {
            Start-Transcript -Path (Join-Path $nightlyDir "nightly_console.log") -Force | Out-Null
            $transcriptStarted = $true
        } catch {
            Write-Host "Warning: failed to start transcript: $($_.Exception.Message)"
        }
    }

    if ($doReconfigure -or -not (Test-Path (Join-Path $resolvedBuildDir "CMakeCache.txt"))) {
        Invoke-Step "ConfigureCMake" {
            if ($doReconfigure) {
                Write-Host "Resetting CMake cache in $resolvedBuildDir for clean reconfigure..."
                Reset-CMakeConfigureState -BuildDirPath $resolvedBuildDir
            }
            cmake -S . -B $resolvedBuildDir -G "Visual Studio 17 2022" -A x64 `
                -DWB_BUILD_DX12_DEMO=ON `
                -DWB_BUILD_STANDALONE=ON `
                -DBUILD_TESTING=ON | Out-Host
            Assert-LastExit "CMake configure failed"
        }
    }

    if ($doCheckIntelliSense) {
        Invoke-Step "IntelliSenseSanity" {
            $intelliSenseArgs = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\scripts\check_intellisense.ps1",
                "-Config", $Config,
                "-BuildDir", $resolvedBuildDir
            )
            if ($doReconfigure) { $intelliSenseArgs += "-Reconfigure" }
            powershell @intelliSenseArgs | Out-Host
            Assert-LastExit "IntelliSense sanity failed"
        }
    }

    Invoke-Step "BuildAll" {
        cmake --build $resolvedBuildDir --config $Config | Out-Host
        Assert-LastExit "Build failed"
    }

    if (-not $doSkipCtest) {
        Invoke-Step "CTest" {
            ctest --test-dir $resolvedBuildDir -C $Config --output-on-failure | Out-Host
            Assert-LastExit "CTest failed"
        }
    }

    Invoke-Step "EventAutomation" {
        powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" -Config $Config -BuildDir $resolvedBuildDir | Out-Host
        Assert-LastExit "Event automation failed"
    }

    Invoke-Step "GlobalFloatSync" {
        powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" -Config $Config -BuildDir $resolvedBuildDir -Scenario global_float_sync | Out-Host
        Assert-LastExit "Global floating sync automation failed"
    }

    Invoke-Step "HostTransferStress" {
        powershell -ExecutionPolicy Bypass -File ".\scripts\run_event_automation.ps1" -Config $Config -BuildDir $resolvedBuildDir -Scenario host_transfer_stress | Out-Host
        Assert-LastExit "Host transfer stress automation failed"
    }

    Invoke-Step "AnalyzeLogs" {
        powershell -ExecutionPolicy Bypass -File ".\scripts\analyze_logs.ps1" | Out-Host
        Assert-LastExit "Log analysis failed"
    }

    if ($doRunEdgeCases) {
        Invoke-Step "EdgeCases" {
            powershell -ExecutionPolicy Bypass -File ".\scripts\test_edge_cases.ps1" -Config $Config -BuildDir $resolvedBuildDir | Out-Host
            Assert-LastExit "Edge-case scenarios failed"
        }
    }

    if ($doCheckResources) {
        Invoke-Step "ResourceCheck" {
            powershell -ExecutionPolicy Bypass -File ".\scripts\check_resources.ps1" -Config $Config -BuildDir $resolvedBuildDir | Out-Host
            Assert-LastExit "Resource check failed"
        }
    }

    if ($doAnalyzeCode) {
        Invoke-Step "CodeAnalysis" {
            powershell -ExecutionPolicy Bypass -File ".\scripts\analyze_code.ps1" | Out-Host
            Assert-LastExit "Code analysis failed"
        }
    }

    if ($doRunAnalysis) {
        Invoke-Step "AIAnalysisReport" {
            $analysisArgs = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\scripts\ai_analysis.ps1",
                "-Config", $Config,
                "-BuildDir", $resolvedBuildDir
            )
            if ($doRunEdgeCases) { $analysisArgs += "-RunEdgeCases" }
            if ($doCheckResources) { $analysisArgs += "-RunResourceCheck" }
            if ($doAnalyzeCode) { $analysisArgs += "-RunCodeAnalysis" }
            if ($doRunPerformance) {
                $analysisArgs += "-RunPerformanceCheck"
                $analysisArgs += "-PerfIterations"
                $analysisArgs += $PerfIterations
                $analysisArgs += "-PerfThresholdPct"
                $analysisArgs += $PerfThresholdPct
                $analysisArgs += "-PerfRenderFrames"
                $analysisArgs += $PerfRenderFrames
                if ($UpdatePerfBaseline) { $analysisArgs += "-UpdatePerfBaseline" }
            }
            powershell @analysisArgs | Out-Host
            Assert-LastExit "AI analysis failed"
        }
    }

    if ($doRunPerformance) {
        Invoke-Step "PerformanceRegression" {
            $perfArgs = @(
                "-ExecutionPolicy", "Bypass",
                "-File", ".\scripts\performance_regression.ps1",
                "-Config", $Config,
                "-BuildDir", $resolvedBuildDir,
                "-Iterations", $PerfIterations,
                "-ThresholdPct", $PerfThresholdPct,
                "-RenderFrames", $PerfRenderFrames
            )
            if ($UpdatePerfBaseline) { $perfArgs += "-UpdateBaseline" }
            powershell @perfArgs | Out-Host
            Assert-LastExit "Performance regression check failed"
        }
    }

    if ($doVisualValidation) {
        Invoke-Step "VisualValidation" {
            powershell -ExecutionPolicy Bypass -File ".\scripts\visual_validation.ps1" -Config $Config -BuildDir $resolvedBuildDir | Out-Host
            Assert-LastExit "Visual validation failed"
        }
    }

    if ($doCheckCrashReporter) {
        Invoke-Step "CrashReporterSmoke" {
            powershell -ExecutionPolicy Bypass -File ".\scripts\check_crash_reporter.ps1" -Config $Config -BuildDir $resolvedBuildDir | Out-Host
            Assert-LastExit "Crash-reporter smoke test failed"
        }
    }

    $summary = Select-String -Path "event_conflicts.log" -Pattern '^\[auto\] summary' | Select-Object -Last 1
    $status = Select-String -Path "event_conflicts.log" -Pattern '^\[auto\] automation checks passed' | Select-Object -Last 1
    if (-not $status) {
        throw "Automation summary did not report pass."
    }

    Write-Host "PASS"
    if ($summary) {
        Write-Host $summary.Line
    }
} catch {
    $runStatus = "Failed"
    $runError = $_.Exception.Message
    Write-Host "FAIL: $runError"
} finally {
    if ($Nightly) {
        if ($transcriptStarted) {
            try { Stop-Transcript | Out-Null } catch {}
        }

        if (-not $nightlyDir) {
            $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
            $nightlyDir = Join-Path $repoRoot (Join-Path $NightlyRoot ("nightly_" + $stamp))
            New-Item -ItemType Directory -Path $nightlyDir -Force | Out-Null
        }

        $artifactCandidates = @(
            "event_conflicts.log",
            "crash_report.log",
            (Join-Path $resolvedBuildDir "Testing\Temporary\LastTest.log")
        )
        foreach ($path in $artifactCandidates) {
            if (Test-Path $path) {
                Copy-Item $path -Destination $nightlyDir -Force
            }
        }

        Get-ChildItem -Path $repoRoot -Filter "dx12_demo_crash_*.dmp" -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStart.AddSeconds(-1) } |
            ForEach-Object {
            Copy-Item $_.FullName -Destination (Join-Path $nightlyDir $_.Name) -Force
        }
        Get-ChildItem -Path $repoRoot -Filter "analysis_report_*.json" -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStart.AddSeconds(-1) } |
            ForEach-Object {
            Copy-Item $_.FullName -Destination (Join-Path $nightlyDir $_.Name) -Force
        }
        Get-ChildItem -Path (Join-Path $repoRoot "artifacts\perf") -Filter "report_*.json" -ErrorAction SilentlyContinue |
            Where-Object { $_.LastWriteTime -ge $runStart.AddSeconds(-1) } |
            ForEach-Object {
            Copy-Item $_.FullName -Destination (Join-Path $nightlyDir $_.Name) -Force
        }

        $manifest = [PSCustomObject]@{
            Timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
            Config = $Config
            Status = $runStatus
            Error = $runError
            Steps = $stepResults
        }
        $manifest | ConvertTo-Json -Depth 8 | Out-File (Join-Path $nightlyDir "nightly_manifest.json") -Encoding UTF8

        if ($NightlyKeep -gt 0) {
            $nightlyBase = Join-Path $repoRoot $NightlyRoot
            if (Test-Path $nightlyBase) {
                $dirs = Get-ChildItem -Path $nightlyBase -Directory | Sort-Object LastWriteTime -Descending
                if ($dirs.Count -gt $NightlyKeep) {
                    $dirs | Select-Object -Skip $NightlyKeep | Remove-Item -Recurse -Force
                }
            }
        }

        Write-Host "Nightly artifacts: $nightlyDir"
    }
}

if ($runStatus -ne "Passed") {
    exit 1
}
