param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [string]$BuildDir = "",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64",
    [string]$Nd2SdkRoot = "C:\Program Files\nd2readsdk-shared",
    [string]$VtkDir = "",
    [string]$ItkDir = ""
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"

function Get-DefaultBuildDir([string]$ConfigurationName) {
    if ($ConfigurationName -eq "Debug") {
        return "build-msvc-debug"
    }
    return "build-msvc-release"
}

function Get-DefaultVtkDir([string]$ConfigurationName, [bool]$InstallTree) {
    $suffix = if ($ConfigurationName -eq "Debug") { "debug" } else { "release" }
    $root = if ($InstallTree) { "opt" } else { "build" }
    return Join-Path $HOME "$root\vtk-9.5.2-qt611-$suffix\lib\cmake\vtk-9.5"
}

function Move-LegacyReleaseVtkTree([string]$RootName) {
    $legacyPath = Join-Path $HOME "$RootName\vtk-9.5.2-qt611"
    $releasePath = Join-Path $HOME "$RootName\vtk-9.5.2-qt611-release"
    if ((Test-Path $legacyPath) -and -not (Test-Path $releasePath)) {
        Write-Host "Migrating existing release VTK path '$legacyPath' -> '$releasePath'"
        Move-Item -LiteralPath $legacyPath -Destination $releasePath
    } elseif ((Test-Path $legacyPath) -and (Test-Path $releasePath)) {
        throw "Both legacy and new release VTK paths exist: '$legacyPath' and '$releasePath'. Resolve the duplicate manually before continuing."
    }
}

function Test-DebugVtkExports([string]$VtkConfigDir) {
    return Test-Path (Join-Path $VtkConfigDir "VTK-targets-debug.cmake")
}

if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Get-DefaultBuildDir $Configuration
}

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

if ($Configuration -eq "Release") {
    Move-LegacyReleaseVtkTree "opt"
    Move-LegacyReleaseVtkTree "build"
}

if ([string]::IsNullOrWhiteSpace($VtkDir)) {
    if ($env:VTK_DIR) {
        $VtkDir = $env:VTK_DIR
    } else {
        $defaultVtkInstallDir = Get-DefaultVtkDir $Configuration $true
        $defaultVtkBuildDir = Get-DefaultVtkDir $Configuration $false
        if (Test-Path $defaultVtkInstallDir) {
            $VtkDir = $defaultVtkInstallDir
        } elseif (Test-Path $defaultVtkBuildDir) {
            $VtkDir = $defaultVtkBuildDir
        }
    }
}

if ([string]::IsNullOrWhiteSpace($ItkDir) -and $env:ITK_DIR) {
    $ItkDir = $env:ITK_DIR
}

if ([string]::IsNullOrWhiteSpace($VtkDir)) {
    $vtkBuildCommand = ".\scripts\build-vtk-msvc.ps1 -Configuration $Configuration"
    throw "VTK_DIR is not set and no matching $Configuration VTK package was found. Run '$vtkBuildCommand' first, or pass -VtkDir explicitly."
}

if (!(Test-Path $VtkDir)) {
    throw "VTK_DIR not found at '$VtkDir'. Build/install VTK first, or pass the directory containing VTKConfig.cmake."
}

if ($Configuration -eq "Debug" -and -not (Test-DebugVtkExports $VtkDir)) {
    throw "Debug builds require a debug VTK package. '$VtkDir' does not expose VTK debug targets. Run '.\scripts\build-vtk-msvc.ps1 -Configuration Debug' or pass a matching debug -VtkDir."
}

if (![string]::IsNullOrWhiteSpace($ItkDir)) {
    if (!(Test-Path $ItkDir)) {
        throw "ITK_DIR not found at '$ItkDir'. Install ITK first, or pass the directory containing ITKConfig.cmake."
    }
}

$configureArgs = @(
    "`"$cmake`"",
    "-S", "`"$repoRoot`"",
    "-B", "`"$repoRoot\$BuildDir`"",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DQt6_DIR=`"$($qtCmakeDir -replace '\\', '/')`"",
    "-DND2SDK_ROOT=`"$($Nd2SdkRoot -replace '\\', '/')`"",
    "-DVTK_DIR=`"$($VtkDir -replace '\\', '/')`""
)
if (![string]::IsNullOrWhiteSpace($ItkDir)) {
    $configureArgs += "-DITK_DIR=`"$($ItkDir -replace '\\', '/')`""
}
$configureCommand = $configureArgs -join " "

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
& (Join-Path $PSScriptRoot "msvc-windeployqt.ps1") -QtRoot $QtRoot -ExePath $exePath
