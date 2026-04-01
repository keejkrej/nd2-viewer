param(
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-msvc-release",
    [string]$OutputDir = "dist",
    [ValidateSet("NSIS", "ZIP")]
    [string]$Generator = "NSIS",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64",
    [string]$Nd2SdkRoot = "C:\Program Files\nd2readsdk-shared"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$cpackConfig = Join-Path $buildPath "CPackConfig.cmake"

if ($Generator -eq "NSIS") {
    $nsisCommand = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if (-not $nsisCommand) {
        $nsisCandidates = @(
            "C:\Program Files (x86)\NSIS\makensis.exe",
            "C:\Program Files (x86)\NSIS\Bin\makensis.exe",
            "C:\Program Files\NSIS\makensis.exe",
            "C:\Program Files\NSIS\Bin\makensis.exe"
        )

        $nsisPath = $nsisCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($nsisPath) {
            $env:PATH = "$(Split-Path -Parent $nsisPath);$env:PATH"
        } else {
            throw "NSIS was not found on PATH or in the standard install locations. Install NSIS and re-run this script, or use -Generator ZIP for a portable archive."
        }
    }
}

& (Join-Path $PSScriptRoot "build-msvc.ps1") `
    -Configuration $Configuration `
    -BuildDir $BuildDir `
    -QtRoot $QtRoot `
    -Nd2SdkRoot $Nd2SdkRoot

if (!(Test-Path $cpackConfig)) {
    throw "CPackConfig.cmake not found at '$cpackConfig'. The configure step did not produce packaging metadata."
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

& cpack --config $cpackConfig -G $Generator -B $outputPath

if ($LASTEXITCODE -ne 0) {
    throw "Packaging failed with exit code $LASTEXITCODE."
}
