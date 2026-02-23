param(
    [string]$BuildDir = "build_dx12",
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Config = "Debug",
    [string]$Target = "",
    [string[]]$Args = @(),
    [switch]$ListOnly,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $repoRoot

$binDir = Join-Path $repoRoot (Join-Path $BuildDir (Join-Path "bin" $Config))
if (-not (Test-Path $binDir)) {
    throw "Build output folder not found: $binDir`nBuild first: cmake --build $BuildDir --config $Config"
}

$apps = Get-ChildItem -Path $binDir -Filter *.exe -File | Sort-Object Name
if ($apps.Count -eq 0) {
    throw "No executables found in $binDir"
}

if ($ListOnly) {
    Write-Host "[apps] $binDir"
    foreach ($app in $apps) {
        Write-Host " - $($app.Name)"
    }
    exit 0
}

function Resolve-Target {
    param(
        [System.IO.FileInfo[]]$Candidates,
        [string]$Name
    )

    if ([string]::IsNullOrWhiteSpace($Name)) {
        Write-Host "[select app]"
        for ($i = 0; $i -lt $Candidates.Count; $i++) {
            Write-Host ("{0,2}) {1}" -f ($i + 1), $Candidates[$i].Name)
        }

        $raw = Read-Host "Choose app number"
        $chosen = 0
        if (-not [int]::TryParse($raw, [ref]$chosen)) {
            throw "Invalid selection: $raw"
        }
        $index = $chosen - 1
        if ($index -lt 0 -or $index -ge $Candidates.Count) {
            throw "Selection out of range: $raw"
        }
        return $Candidates[$index]
    }

    $nameWithExt = if ($Name.EndsWith(".exe", [System.StringComparison]::OrdinalIgnoreCase)) { $Name } else { "$Name.exe" }
    $exact = $Candidates | Where-Object { $_.Name -ieq $nameWithExt }
    if ($exact.Count -eq 1) {
        return $exact[0]
    }
    if ($exact.Count -gt 1) {
        throw "Ambiguous exact matches for $nameWithExt"
    }

    $partial = $Candidates | Where-Object { $_.BaseName -like "*$Name*" }
    if ($partial.Count -eq 1) {
        return $partial[0]
    }
    if ($partial.Count -gt 1) {
        Write-Host "Multiple matches:"
        foreach ($item in $partial) {
            Write-Host " - $($item.Name)"
        }
        throw "Target '$Name' is ambiguous"
    }

    throw "Target '$Name' not found in $binDir"
}

$targetExe = Resolve-Target -Candidates $apps -Name $Target
Write-Host "[launch] $($targetExe.FullName)"
$launchArgs = @()
if ($Args) {
    $launchArgs = @(
        $Args | Where-Object {
            $_ -ne $null -and -not [string]::IsNullOrWhiteSpace($_)
        }
    )
}

$startParams = @{
    FilePath = $targetExe.FullName
    WorkingDirectory = $repoRoot
    PassThru = $true
}
if ($launchArgs.Count -gt 0) {
    $startParams.ArgumentList = $launchArgs
    Write-Host "[args] $($launchArgs -join ' ')"
}

$process = Start-Process @startParams
Write-Host "[pid] $($process.Id)"

if ($Wait) {
    $process.WaitForExit()
    exit $process.ExitCode
}
