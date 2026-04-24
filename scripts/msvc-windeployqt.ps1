# Bundle Qt DLLs next to the MSVC-built nd2-viewer.exe (mirrors scripts/macos-macdeployqt.sh).
# Invoked from package-msvc.ps1 (and can be run manually after a build).
# QtRoot is the Qt prefix: e.g. repo\vcpkg_installed\x64-windows.
# Usage: .\scripts\msvc-windeployqt.ps1 -QtRoot ".\vcpkg_installed\x64-windows" -ExePath "...\bin\nd2-viewer.exe"
param(
    [Parameter(Mandatory = $true)]
    [string]$QtRoot,
    [Parameter(Mandatory = $true)]
    [string]$ExePath
)

$ErrorActionPreference = "Stop"

function Get-IcuSourceDirectory([string]$QtRootPath, [string]$WindeployqtPath, [string]$ExecutablePath) {
    $qtBinDir = Join-Path $QtRootPath "bin"
    $qtDebugBinDir = Join-Path $QtRootPath "debug\bin"
    $candidateDirs = @()

    if (Test-Path $qtDebugBinDir) {
        $candidateDirs += $qtDebugBinDir
    }
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

function Test-DebugQtDeployment([string]$TargetDir) {
    return (Test-Path (Join-Path $TargetDir "Qt6Cored.dll")) -or
        (Test-Path (Join-Path $TargetDir "Qt6Multimediad.dll"))
}

function Copy-DirectoryContents([string]$SourceDir, [string]$TargetDir) {
    if (!(Test-Path $SourceDir)) {
        return
    }

    New-Item -ItemType Directory -Path $TargetDir -Force | Out-Null
    Get-ChildItem -LiteralPath $SourceDir -Force | Copy-Item -Destination $TargetDir -Recurse -Force
}

function Deploy-DebugQtFromVcpkg([string]$QtRootPath, [string]$TargetDir) {
    $debugQtRoot = Join-Path $QtRootPath "debug\Qt6"
    $debugBinRoot = Join-Path $QtRootPath "debug\bin"
    $debugPluginRoot = Join-Path $debugQtRoot "plugins"

    if (!(Test-Path $debugBinRoot)) {
        throw "msvc-windeployqt: debug Qt bin directory not found at '$debugBinRoot'."
    }
    if (!(Test-Path $debugPluginRoot)) {
        throw "msvc-windeployqt: debug Qt plugin directory not found at '$debugPluginRoot'."
    }

    Write-Host "msvc-windeployqt: copying debug Qt DLLs from '$debugBinRoot'..."
    Get-ChildItem -LiteralPath $debugBinRoot -Filter "Qt6*d.dll" -Force |
        Copy-Item -Destination $TargetDir -Force

    Write-Host "msvc-windeployqt: copying debug Qt plugins from '$debugPluginRoot'..."
    foreach ($pluginDir in Get-ChildItem -LiteralPath $debugPluginRoot -Directory) {
        Copy-DirectoryContents -SourceDir $pluginDir.FullName -TargetDir (Join-Path $TargetDir $pluginDir.Name)
    }
}

$windeployqt = $null
foreach ($candidate in @(
        (Join-Path $QtRoot "tools\Qt6\bin\windeployqt.exe"),
        (Join-Path $QtRoot "bin\windeployqt.exe")
    )) {
    if (Test-Path $candidate) {
        $windeployqt = $candidate
        break
    }
}
if ($null -eq $windeployqt) {
    throw "msvc-windeployqt: windeployqt not found under '$QtRoot' (tried tools\Qt6\bin and bin). Check -QtRoot."
}
if (!(Test-Path $ExePath)) {
    throw "msvc-windeployqt: executable not found at '$ExePath'."
}

$targetDir = Split-Path -Parent $ExePath
$isDebugDeployment = Test-DebugQtDeployment -TargetDir $targetDir
if ($isDebugDeployment) {
    Write-Host "msvc-windeployqt: deploying debug Qt runtime for '$ExePath'..."
    Deploy-DebugQtFromVcpkg -QtRootPath $QtRoot -TargetDir $targetDir
} else {
    Write-Host "msvc-windeployqt: running windeployqt for '$ExePath'..."
    $oldPath = $env:PATH
    $qtRuntimeBinDir = Join-Path $QtRoot "bin"
    try {
        if (Test-Path $qtRuntimeBinDir) {
            $env:PATH = "$qtRuntimeBinDir;$oldPath"
        }
        & $windeployqt --no-translations --no-compiler-runtime --release $ExePath
    } finally {
        $env:PATH = $oldPath
    }
    if ($LASTEXITCODE -ne 0) {
        throw "msvc-windeployqt: windeployqt failed with exit code $LASTEXITCODE."
    }
}

$icuSourceDir = Get-IcuSourceDirectory -QtRootPath $QtRoot -WindeployqtPath $windeployqt -ExecutablePath $ExePath
Write-Host "msvc-windeployqt: copying ICU runtime from '$icuSourceDir'..."
Copy-IcuRuntime -SourceDir $icuSourceDir -TargetDir $targetDir
Assert-RequiredRuntimeFiles -TargetDir $targetDir
