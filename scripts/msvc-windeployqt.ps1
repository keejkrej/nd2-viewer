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

function Get-IcuSourceDirectory([string]$QtRootPath, [string]$WindeployqtPath, [string]$ExecutablePath) {
    $qtBinDir = Join-Path $QtRootPath "bin"
    $candidateDirs = @()

    if (Test-Path $qtBinDir) {
        $candidateDirs += $qtBinDir
    }

    try {
        $mappingOutput = & $WindeployqtPath --list mapping $ExecutablePath 2>$null
        foreach ($line in $mappingOutput) {
            if ($line -match '^"(?<source>[^"]+icuuc\.dll)"\s+"(?<target>[^"]+)"$') {
                $candidateDirs += Split-Path -Parent $matches.source
            }
        }
    } catch {
    }

    if ([Environment]::Is64BitOperatingSystem) {
        $candidateDirs += "$env:WINDIR\System32"
    }
    $candidateDirs += "$env:WINDIR\SysWOW64"

    foreach ($dir in ($candidateDirs | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique)) {
        if ((Test-Path (Join-Path $dir "icu.dll")) -and
            (Test-Path (Join-Path $dir "icuin.dll")) -and
            (Test-Path (Join-Path $dir "icuuc.dll"))) {
            return $dir
        }
    }

    throw "msvc-windeployqt: could not find a complete ICU runtime set (icu.dll, icuin.dll, icuuc.dll) in Qt or Windows runtime directories."
}

function Copy-IcuRuntime([string]$SourceDir, [string]$TargetDir) {
    foreach ($name in @("icu.dll", "icuin.dll", "icuuc.dll")) {
        $source = Join-Path $SourceDir $name
        if (!(Test-Path $source)) {
            throw "msvc-windeployqt: ICU runtime '$source' was not found."
        }

        Copy-Item -LiteralPath $source -Destination (Join-Path $TargetDir $name) -Force
    }
}

function Assert-RequiredRuntimeFiles([string]$TargetDir) {
    $coreCandidates = @("Qt6Core.dll", "Qt6Cored.dll")
    if (-not ($coreCandidates | Where-Object { Test-Path (Join-Path $TargetDir $_) } | Select-Object -First 1)) {
        throw "msvc-windeployqt: required runtime files are missing from '$TargetDir': Qt6Core.dll or Qt6Cored.dll"
    }

    $requiredFiles = @("icu.dll", "icuin.dll", "icuuc.dll")
    $missing = @($requiredFiles | Where-Object { !(Test-Path (Join-Path $TargetDir $_)) })
    if ($missing.Count -gt 0) {
        throw "msvc-windeployqt: required runtime files are missing from '$TargetDir': $($missing -join ', ')"
    }
}

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

$targetDir = Split-Path -Parent $ExePath
$icuSourceDir = Get-IcuSourceDirectory -QtRootPath $QtRoot -WindeployqtPath $windeployqt -ExecutablePath $ExePath
Write-Host "msvc-windeployqt: copying ICU runtime from '$icuSourceDir'..."
Copy-IcuRuntime -SourceDir $icuSourceDir -TargetDir $targetDir
Assert-RequiredRuntimeFiles -TargetDir $targetDir
