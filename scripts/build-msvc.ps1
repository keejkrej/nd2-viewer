param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [string]$BuildDir = "",
    [string]$Nd2SdkRoot = "C:\Program Files\nd2readsdk-shared",
    [string]$VcpkgRoot = "",
    [string]$VcpkgTriplet = "x64-windows"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "vcpkg-common.ps1")
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"

function Get-DefaultBuildDir([string]$ConfigurationName) {
    if ($ConfigurationName -eq "Debug") {
        return "build-msvc-debug"
    }
    return "build-msvc-release"
}

function Resolve-ToolPath([string]$Name, [string]$InstallHint) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "$Name was not found on PATH. $InstallHint"
}

function Reset-BuildDirIfStalePackageCache([string]$BuildPath, [string]$ExpectedPackagePrefix) {
    $cachePath = Join-Path $BuildPath "CMakeCache.txt"
    if (!(Test-Path $cachePath)) {
        return
    }

    $repoRootFull = [System.IO.Path]::GetFullPath($repoRoot).TrimEnd('\', '/')
    $buildPathFull = [System.IO.Path]::GetFullPath($BuildPath).TrimEnd('\', '/')
    if (-not $buildPathFull.StartsWith($repoRootFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove build directory outside repo root: '$buildPathFull'."
    }

    $expectedPrefixFull = [System.IO.Path]::GetFullPath($ExpectedPackagePrefix).TrimEnd('\', '/')
    $staleLines = @()
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -notmatch '^(Qt6[^:]*_DIR|VTK_DIR|ITK_DIR|libCZI_DIR):[^=]*=(?<value>.+)$') {
            continue
        }

        $value = $matches.value.Trim()
        if ([string]::IsNullOrWhiteSpace($value) -or $value.EndsWith("-NOTFOUND")) {
            continue
        }

        $normalizedValue = [System.IO.Path]::GetFullPath(($value -replace '/', '\')).TrimEnd('\', '/')
        if (-not $normalizedValue.StartsWith($expectedPrefixFull + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            $staleLines += $line
        }
    }

    if ($staleLines.Count -eq 0) {
        return
    }

    Write-Host "Removing stale CMake build directory '$buildPathFull' because it caches non-vcpkg package paths:"
    $staleLines | Select-Object -First 5 | ForEach-Object { Write-Host "  $_" }
    Remove-Item -LiteralPath $buildPathFull -Recurse -Force
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Get-DefaultBuildDir $Configuration
}
$buildPath = [System.IO.Path]::GetFullPath((Join-Path $repoRoot $BuildDir))

if (!(Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}

$cmake = Resolve-ToolPath "cmake.exe" "Install CMake, or add an existing CMake install to PATH."
$ninja = Resolve-ToolPath "ninja.exe" "Install Ninja, or add an existing Ninja install to PATH."

if (!(Test-Path $Nd2SdkRoot)) {
    throw "ND2 SDK root not found at '$Nd2SdkRoot'."
}
$nd2SdkHeader = Join-Path $Nd2SdkRoot "include\Nd2ReadSdk.h"
$nd2SdkDll = Join-Path $Nd2SdkRoot "bin\nd2readsdk-shared.dll"
if (!(Test-Path $nd2SdkHeader)) {
    throw "ND2 SDK header not found at '$nd2SdkHeader'."
}
if (!(Test-Path $nd2SdkDll)) {
    throw "ND2 shared SDK DLL not found at '$nd2SdkDll'."
}

$resolvedVcpkgRoot = Resolve-VcpkgRoot -Explicit $VcpkgRoot
$vcpkgExe = Join-Path $resolvedVcpkgRoot "vcpkg.exe"
if (!(Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found at '$vcpkgExe'."
}
$toolchainFile = Join-Path $resolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$vcpkgInstalledDir = Join-Path $repoRoot "vcpkg_installed"

$qtRootForDeploy = Join-Path $vcpkgInstalledDir $VcpkgTriplet

$vcpkgInstallCommand = @(
    "`"$vcpkgExe`"",
    "install",
    "--triplet", $VcpkgTriplet,
    "--vcpkg-root", "`"$resolvedVcpkgRoot`""
) -join " "

$configureCommand = @(
    "`"$cmake`"",
    "-S", "`"$repoRoot`"",
    "-B", "`"$buildPath`"",
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=`"$($ninja -replace '\\', '/')`"",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=`"$($toolchainFile -replace '\\', '/')`"",
    "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet",
    "-DVCPKG_INSTALLED_DIR=`"$($vcpkgInstalledDir -replace '\\', '/')`"",
    "-DQt6_DIR:PATH=",
    "-DVTK_DIR:PATH=",
    "-DITK_DIR:PATH=",
    "-DCMAKE_PREFIX_PATH:STRING=",
    "-DND2SDK_ROOT=`"$($Nd2SdkRoot -replace '\\', '/')`""
) -join " "

$buildCommand = @(
    "`"$cmake`"",
    "--build", "`"$buildPath`"",
    "--config", $Configuration,
    "-j", "8"
) -join " "

Write-Host "vcpkg: installing manifest dependencies (Qt, VTK, ITK, libczi, ...) triplet=$VcpkgTriplet"
Write-Host "vcpkg root: $resolvedVcpkgRoot"

Reset-BuildDirIfStalePackageCache -BuildPath $buildPath -ExpectedPackagePrefix $qtRootForDeploy

$command = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && cd /d `"$repoRoot`" && $vcpkgInstallCommand && $configureCommand && $buildCommand"
cmd.exe /c $command

if ($LASTEXITCODE -ne 0) {
    throw "MSVC configure/build failed with exit code $LASTEXITCODE."
}

$exePath = Join-Path $buildPath "bin\nd2-viewer.exe"
& (Join-Path $PSScriptRoot "msvc-windeployqt.ps1") -QtRoot $qtRootForDeploy -ExePath $exePath
