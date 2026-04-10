param(
    [ValidateSet("Debug", "Release", "RelWithDebInfo", "MinSizeRel")]
    [string]$Configuration = "Debug",
    [string]$BuildDir = "build-msvc",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64",
    [string]$Nd2SdkRoot = "C:\Program Files\nd2readsdk-shared",
    [string]$VtkDir = $env:VTK_DIR
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"

if (!(Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}

if (!(Test-Path $cmake)) {
    throw "CMake not found at '$cmake'."
}

$qtCmakeDir = Join-Path $QtRoot "lib\cmake\Qt6"
if (!(Test-Path $qtCmakeDir)) {
    throw "Qt6Config.cmake not found under '$qtCmakeDir'."
}

if (!(Test-Path $Nd2SdkRoot)) {
    throw "ND2 SDK root not found at '$Nd2SdkRoot'."
}

if ($VtkDir -and !(Test-Path $VtkDir)) {
    throw "VTK_DIR not found at '$VtkDir'. Build/install VTK first, or pass the directory containing VTKConfig.cmake."
}

$configureCommand = @(
    "`"$cmake`"",
    "-S", "`"$repoRoot`"",
    "-B", "`"$repoRoot\$BuildDir`"",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DQt6_DIR=`"$($qtCmakeDir -replace '\\', '/')`"",
    "-DND2SDK_ROOT=`"$($Nd2SdkRoot -replace '\\', '/')`""
) -join " "

if ($VtkDir) {
    $configureCommand += " -DVTK_DIR=`"$($VtkDir -replace '\\', '/')`""
}

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
