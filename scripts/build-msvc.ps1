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

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Get-DefaultBuildDir $Configuration
}

if (!(Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}

$cmake = Resolve-ToolPath "cmake.exe" "Install CMake, or add an existing CMake install to PATH."
$ninja = Resolve-ToolPath "ninja.exe" "Install Ninja, or add an existing Ninja install to PATH."

if (!(Test-Path $Nd2SdkRoot)) {
    throw "ND2 SDK root not found at '$Nd2SdkRoot'."
}

$resolvedVcpkgRoot = Resolve-VcpkgRoot -Explicit $VcpkgRoot
$vcpkgExe = Join-Path $resolvedVcpkgRoot "vcpkg.exe"
if (!(Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found at '$vcpkgExe'."
}
$toolchainFile = Join-Path $resolvedVcpkgRoot "scripts\buildsystems\vcpkg.cmake"

$qtRootForDeploy = Join-Path $resolvedVcpkgRoot "installed\$VcpkgTriplet"

$configureCommand = @(
    "`"$cmake`"",
    "-S", "`"$repoRoot`"",
    "-B", "`"$repoRoot\$BuildDir`"",
    "-G", "Ninja",
    "-DCMAKE_MAKE_PROGRAM=`"$($ninja -replace '\\', '/')`"",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=`"$($toolchainFile -replace '\\', '/')`"",
    "-DVCPKG_TARGET_TRIPLET=$VcpkgTriplet",
    "-DND2SDK_ROOT=`"$($Nd2SdkRoot -replace '\\', '/')`""
) -join " "

$buildCommand = @(
    "`"$cmake`"",
    "--build", "`"$repoRoot\$BuildDir`"",
    "--config", $Configuration,
    "-j", "8"
) -join " "

$command = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && $configureCommand && $buildCommand"
cmd.exe /c $command

if ($LASTEXITCODE -ne 0) {
    throw "MSVC configure/build failed with exit code $LASTEXITCODE."
}

$exePath = Join-Path $repoRoot "$BuildDir\bin\nd2-viewer.exe"
& (Join-Path $PSScriptRoot "msvc-windeployqt.ps1") -QtRoot $qtRootForDeploy -ExePath $exePath
