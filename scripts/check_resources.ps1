param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [string]$BuildDir = "build_dx12",
    [int]$Iterations = 5,
    [int]$MaxPeakMB = 600
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$exe = Join-Path $repoRoot (Join-Path $BuildDir (Join-Path "bin\$Config" "dx12_demo.exe"))
if (!(Test-Path $exe)) {
    throw "Executable not found: $exe"
}

$maxPeak = 0.0
$rows = @()
for ($i = 1; $i -le $Iterations; $i++) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $exe
    $psi.WorkingDirectory = $repoRoot
    $psi.UseShellExecute = $false
    $psi.EnvironmentVariables["DF_EVENT_CONSOLE"] = "0"
    $psi.EnvironmentVariables["DF_EVENT_VERBOSE"] = "0"
    $psi.EnvironmentVariables["DF_AUTOMATE_EVENTS"] = "1"
    $psi.EnvironmentVariables["DF_AUTOMATION_SCENARIO"] = "mixed"

    $p = New-Object System.Diagnostics.Process
    $p.StartInfo = $psi
    [void]$p.Start()
    $peakObserved = 0.0
    while (-not $p.HasExited) {
        try {
            $p.Refresh()
            $current = [math]::Round($p.WorkingSet64 / 1MB, 2)
            $peakObserved = [math]::Max($peakObserved, $current)
        } catch {
            # Ignore transient query failures while process exits.
        }
        Start-Sleep -Milliseconds 20
    }
    $null = $p.WaitForExit(30000)
    if (!$p.HasExited) {
        $p.Kill()
        throw "Process timeout at iteration $i"
    }
    if ($p.ExitCode -ne 0) {
        throw "Automation exit code $($p.ExitCode) at iteration $i"
    }

    $p.Refresh()
    $peakReported = [math]::Round($p.PeakWorkingSet64 / 1MB, 2)
    $peakMB = [math]::Max($peakObserved, $peakReported)
    $maxPeak = [math]::Max($maxPeak, $peakMB)
    $rows += [PSCustomObject]@{
        Iteration = $i
        PeakWorkingSetMB = $peakMB
    }
    $p.Dispose()
}

$avg = [math]::Round((($rows | Measure-Object -Property PeakWorkingSetMB -Average).Average), 2)
Write-Host "Resource summary: iterations=$Iterations avgPeakMB=$avg maxPeakMB=$maxPeak"
if ($maxPeak -gt $MaxPeakMB) {
    throw "Peak working set too high: $maxPeak MB > $MaxPeakMB MB"
}

Write-Host "Resource check passed."
