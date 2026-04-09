# Bundle Qt DLLs next to the MSVC-built nd2-viewer.exe (mirrors scripts/macos-macdeployqt.sh).
# Invoked from package-msvc.ps1 (and can be run manually after a build).
# Usage: .\scripts\msvc-windeployqt.ps1 -QtRoot "C:\Qt\6.x\msvc2022_64" -ExePath "...\bin\nd2-viewer.exe"
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExePath
)

$ErrorActionPreference = "Stop"

$windeployqt = Join-Path $QtRoot "bin\windeployqt.exe"
if (!(Test-Path $windeployqt)) {
    throw "msvc-windeployqt: windeployqt not found at '$windeployqt'. Check -QtRoot."
}
if (!(Test-Path $ExePath)) {
    throw "msvc-windeployqt: executable not found at '$ExePath'."
}

Write-Host "msvc-windeployqt: running windeployqt for '$ExePath'..."
& $windeployqt --no-translations --no-compiler-runtime $ExePath
if ($LASTEXITCODE -ne 0) {
    throw "msvc-windeployqt: windeployqt failed with exit code $LASTEXITCODE."
}
