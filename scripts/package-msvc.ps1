param(
    [ValidateSet("Release")]
    [string]$Configuration = "Release",
    [string]$BuildDir = "build-msvc-release",
    [string]$OutputDir = "dist",
    [ValidateSet("NSIS", "ZIP")]
    [string]$Generator = "NSIS"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$buildPath = Join-Path $repoRoot $BuildDir
$outputPath = Join-Path $repoRoot $OutputDir
$cpackConfig = Join-Path $buildPath "CPackConfig.cmake"
$exePath = Join-Path $buildPath "bin\nd2-viewer.exe"
$makensisPath = $null
$requiredRuntimeFiles = @(
    "Qt6Core.dll",
    "icu.dll",
    "icuin.dll",
    "icuuc.dll"
)

if (!(Test-Path $buildPath)) {
    throw "Release build directory '$buildPath' was not found. Run .\scripts\build-msvc.ps1 -Configuration Release first."
}

if (!(Test-Path $exePath)) {
    throw "Release executable '$exePath' was not found. Run .\scripts\build-msvc.ps1 -Configuration Release first."
}

$missingRuntimeFiles = @($requiredRuntimeFiles | Where-Object { !(Test-Path (Join-Path (Split-Path -Parent $exePath) $_)) })
if ($missingRuntimeFiles.Count -gt 0) {
    throw "Release runtime payload is incomplete. Missing files in '$($buildPath)\bin': $($missingRuntimeFiles -join ', '). Run .\scripts\build-msvc.ps1 -Configuration Release again before packaging."
}

if (!(Test-Path $cpackConfig)) {
    throw "CPackConfig.cmake not found at '$cpackConfig'. Run .\scripts\build-msvc.ps1 -Configuration Release first so CMake generates packaging metadata."
}

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
