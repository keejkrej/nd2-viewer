param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$BuildDir = "build-msvc"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$exePath = Join-Path $repoRoot "$BuildDir\bin\nd2-viewer.exe"

if (!(Test-Path $exePath)) {
    throw "Executable not found at '$exePath'. Run scripts/build-msvc.ps1 first."
}

Start-Process -FilePath $exePath
