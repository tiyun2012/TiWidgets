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

$impl = Join-Path $PSScriptRoot "launch_app.ps1"
if (-not (Test-Path $impl)) {
    throw "Missing implementation script: $impl"
}

$forward = @{
    BuildDir = $BuildDir
    Config = $Config
    Target = $Target
    Args = $Args
}
if ($ListOnly) { $forward.ListOnly = $true }
if ($Wait) { $forward.Wait = $true }

& $impl @forward
exit $LASTEXITCODE
