param(
    [string]$Root = "."
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path $Root
Set-Location $repoRoot

$issues = @()
$targetPaths = @(
    (Join-Path $repoRoot "main.cpp"),
    (Join-Path $repoRoot "widgetsBase")
)

$cppFiles = @()
foreach ($path in $targetPaths) {
    if (Test-Path $path) {
        if ((Get-Item $path).PSIsContainer) {
            $cppFiles += Get-ChildItem -Path $path -Recurse -Include *.cpp,*.h -File
        } else {
            $cppFiles += Get-Item $path
        }
    }
}

foreach ($file in $cppFiles) {
    $content = Get-Content $file.FullName -Raw

    if ($content -match 'catch\s*\(\s*\.\.\.\s*\)') {
        $lines = Get-Content $file.FullName
        $catchLines = Select-String -Path $file.FullName -Pattern 'catch\s*\(\s*\.\.\.\s*\)' -CaseSensitive
        foreach ($match in $catchLines) {
            $lineIndex = [Math]::Max(0, $match.LineNumber - 1)
            $windowEnd = [Math]::Min($lines.Count - 1, $lineIndex + 8)
            $window = ($lines[$lineIndex..$windowEnd] -join "`n")
            $isCrashGuard = $window -match 'AppendRuntimeError|PostQuitMessage|WriteCrashArtifacts|SetUnhandledExceptionFilter'
            if (-not $isCrashGuard) {
                $issues += "Catch-all handler in $($file.FullName):$($match.LineNumber)"
            }
        }
    }

    if ($content -match '\bnew\b' -and $content -notmatch 'std::unique_ptr|std::shared_ptr|ComPtr') {
        $issues += "Raw allocation pattern in $($file.FullName)"
    }

    if ($file.Extension -eq ".cpp" -and $file.Name -match 'dx12' -and $content -match 'Create[A-Za-z0-9_]*\(' -and $content -notmatch 'ThrowIfFailed|FAILED|SUCCEEDED') {
        $issues += "Potential unchecked DX12 create call in $($file.FullName)"
    }
}

if ($issues.Count -gt 0) {
    Write-Host "Code analysis issues: $($issues.Count)"
    $issues | ForEach-Object { Write-Host " - $_" }
    exit 1
}

Write-Host "Code analysis passed."
