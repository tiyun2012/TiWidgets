param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [string]$BuildDir = 'build_dx12',
    [switch]$CapturePreviews
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot
$runDir = Join-Path $repoRoot ("artifacts/milestones/{0}_{1}" -f (Get-Date -Format 'yyyyMMdd_HHmmss_fff'), $Config)
New-Item -ItemType Directory -Force -Path $runDir | Out-Null
$latest = Join-Path $repoRoot "artifacts/milestones/latest-$Config.json"
$report = [ordered]@{
    Configuration = $Config
    BuildDirectory = $BuildDir
    StartedAt = (Get-Date).ToString('o')
    Status = 'Running'
    Artifacts = $runDir
    Milestones = @(
        [ordered]@{ Id = 'M1'; Name = 'Configure and build'; Status = 'Pending'; DurationMs = 0; Log = 'M1.log'; Error = '' },
        [ordered]@{ Id = 'M2'; Name = 'Docking and resize regression'; Status = 'Pending'; DurationMs = 0; Log = 'M2.log'; Error = '' },
        [ordered]@{ Id = 'M3'; Name = 'Workspace, themes, native hosts and rendering'; Status = 'Pending'; DurationMs = 0; Log = 'M3.log'; Error = '' },
        [ordered]@{ Id = 'M4'; Name = 'Visual previews and GPU batch comparison'; Status = 'Pending'; DurationMs = 0; Log = 'M4.log'; Error = '' }
    )
}

function Save-Report {
    $json = $report | ConvertTo-Json -Depth 8
    $json | Set-Content -LiteralPath (Join-Path $runDir 'report.json') -Encoding UTF8
    $json | Set-Content -LiteralPath $latest -Encoding UTF8
    $lines = @("# UI milestones - $Config", '', "Status: $($report.Status)", '', '| Milestone | Status | Duration |', '| --- | --- | --- |')
    foreach ($m in $report.Milestones) {
        $lines += "| $($m.Id) - $($m.Name) | $($m.Status) | $($m.DurationMs) ms |"
        if ($m.Error) { $lines += "`n$($m.Id): $($m.Error)`n" }
    }
    $lines += @('', 'Detailed output: M1.log through M4.log. Test results: core.xml and workspace.xml.',
        'Previews require an unobstructed interactive Windows desktop; their creation alone is not visual approval.')
    $lines | Set-Content -LiteralPath (Join-Path $runDir 'report.md') -Encoding UTF8
}

function Invoke-Milestone([int]$Index, [scriptblock]$Action) {
    $m = $report.Milestones[$Index]
    $m.Status = 'Running'
    Save-Report
    Write-Host "[$($m.Id)/M4] $($m.Name) - Running"
    Write-Progress -Activity 'TiWidgets UI milestones' -Status $m.Name -PercentComplete ($Index * 25)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        & $Action *>&1 | Tee-Object -FilePath (Join-Path $runDir $m.Log) | Out-Host
        $m.Status = 'Passed'
    } catch {
        $m.Status = 'Failed'
        $m.Error = $_.Exception.Message
        $m.Error | Add-Content -LiteralPath (Join-Path $runDir $m.Log)
        throw
    } finally {
        $m.DurationMs = $timer.ElapsedMilliseconds
        Save-Report
        Write-Host "[$($m.Id)/M4] $($m.Status) ($($m.DurationMs) ms)"
    }
}

function Assert-Exit([string]$Name) {
    if ($LASTEXITCODE -ne 0) { throw "$Name failed with exit code $LASTEXITCODE" }
}

try {
    Save-Report
    Invoke-Milestone 0 {
        if (-not (Test-Path -LiteralPath (Join-Path $BuildDir 'CMakeCache.txt'))) {
            cmake -S . -B $BuildDir -G 'Visual Studio 17 2022' -A x64 -DWB_BUILD_DX12_DEMO=ON -DWB_BUILD_STANDALONE=ON -DBUILD_TESTING=ON
            Assert-Exit 'Configure'
        } else {
            cmake -S . -B $BuildDir -DWB_BUILD_DX12_DEMO=ON -DWB_BUILD_STANDALONE=ON -DBUILD_TESTING=ON
            Assert-Exit 'Configure'
        }
        cmake --build $BuildDir --config $Config --parallel
        Assert-Exit 'Build'
    }
    Invoke-Milestone 1 {
        ctest --test-dir $BuildDir -C $Config -LE ui --output-on-failure --no-tests=error --output-junit (Join-Path $runDir 'core.xml')
        Assert-Exit 'Docking regression'
    }
    Invoke-Milestone 2 {
        ctest --test-dir $BuildDir -C $Config -L ui --output-on-failure --no-tests=error --output-junit (Join-Path $runDir 'workspace.xml')
        Assert-Exit 'Workspace regression'
        $testArtifacts = Join-Path $repoRoot "$BuildDir/test-artifacts"
        if (Test-Path -LiteralPath $testArtifacts) { Copy-Item -LiteralPath $testArtifacts -Destination (Join-Path $runDir 'test-artifacts') -Recurse }
    }
    if ($CapturePreviews) {
        Invoke-Milestone 3 {
            foreach ($theme in @('dark', 'light', 'slate')) {
                powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/preview_ui.ps1" -Config $Config -BuildDir $BuildDir -Theme $theme -OutputPath (Join-Path $runDir "preview-$theme.png")
                Assert-Exit "$theme preview"
            }
            powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/preview_ui.ps1" -Config $Config -BuildDir $BuildDir -Width 800 -Height 560 -OutputPath (Join-Path $runDir 'preview-compact.png')
            Assert-Exit 'Compact preview'
            powershell -NoProfile -ExecutionPolicy Bypass -File "$PSScriptRoot/preview_ui.ps1" -Config $Config -BuildDir $BuildDir -BatchStress -OutputPath (Join-Path $runDir 'preview-batch.png')
            Assert-Exit 'Batch preview'
            # Caption, cursor and desktop borders are excluded from these client captures.
            $normalHash = (Get-FileHash -LiteralPath (Join-Path $runDir 'preview-dark.png')).Hash
            $batchHash = (Get-FileHash -LiteralPath (Join-Path $runDir 'preview-batch.png')).Hash
            if ($normalHash -ne $batchHash) { throw 'Normal and multi-batch previews differ. Inspect the two images for rendering errors or desktop occlusion.' }
            Write-Output 'Normal and multi-batch rendering match exactly.'
        }
    } else {
        $report.Milestones[3].Status = 'Skipped'
        $report.Milestones[3].Error = 'Run with -CapturePreviews on an interactive desktop.'
    }
    $report.Status = 'Passed'
} catch {
    $report.Status = 'Failed'
    Write-Host $_.Exception.Message -ForegroundColor Red
} finally {
    $report['FinishedAt'] = (Get-Date).ToString('o')
    Save-Report
    Write-Progress -Activity 'TiWidgets UI milestones' -Completed
    Write-Host "Milestone report: $runDir/report.md"
    Write-Host "Live JSON status: $latest"
}
if ($report.Status -eq 'Failed') { exit 1 }
