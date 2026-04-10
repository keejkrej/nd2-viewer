param(
    [ValidateSet("Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-msvc-release",
    [string]$OutputDir = "dist",
    [ValidateSet("NSIS", "ZIP")]
    [string]$Generator = "NSIS",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64",
    [string]$Nd2SdkRoot = "C:\Program Files\nd2readsdk-shared",
    [string]$VtkDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$cpackConfig = Join-Path $buildPath "CPackConfig.cmake"
$makensisPath = $null

if ($Generator -eq "NSIS") {
    $nsisCommand = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($nsisCommand) {
        $makensisPath = $nsisCommand.Source
    } else {
        $nsisCandidates = @(
            "C:\Program Files (x86)\NSIS\makensis.exe",
            "C:\Program Files (x86)\NSIS\Bin\makensis.exe",
            "C:\Program Files\NSIS\makensis.exe",
            "C:\Program Files\NSIS\Bin\makensis.exe"
        )

        $makensisPath = $nsisCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
        if ($makensisPath) {
            $env:PATH = "$(Split-Path -Parent $makensisPath);$env:PATH"
        } else {
            throw "NSIS was not found on PATH or in the standard install locations. Install NSIS and re-run this script, or use -Generator ZIP for a portable archive."
        }
    }
}

& (Join-Path $PSScriptRoot "build-msvc.ps1") `
    -Configuration $Configuration `
    -BuildDir $BuildDir `
    -QtRoot $QtRoot `
    -Nd2SdkRoot $Nd2SdkRoot `
    -VtkDir $VtkDir

$exePath = Join-Path $buildPath "bin\nd2-viewer.exe"
& (Join-Path $PSScriptRoot "msvc-windeployqt.ps1") -QtRoot $QtRoot -ExePath $exePath

if (!(Test-Path $cpackConfig)) {
    throw "CPackConfig.cmake not found at '$cpackConfig'. The configure step did not produce packaging metadata."
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null

if ($Generator -eq "NSIS") {
    $previousMakensisPath = $env:ND2_VIEWER_MAKENSIS_EXE
    $env:ND2_VIEWER_MAKENSIS_EXE = $makensisPath
}

try {
    & cpack --config $cpackConfig -G $Generator -B $outputPath
} finally {
    if ($Generator -eq "NSIS") {
        if ($null -eq $previousMakensisPath) {
            Remove-Item Env:ND2_VIEWER_MAKENSIS_EXE -ErrorAction SilentlyContinue
        } else {
            $env:ND2_VIEWER_MAKENSIS_EXE = $previousMakensisPath
        }
    }
}

if ($LASTEXITCODE -ne 0) {
    throw "Packaging failed with exit code $LASTEXITCODE."
}
