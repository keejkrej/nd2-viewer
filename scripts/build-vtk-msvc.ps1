param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [string]$VtkRef = "v9.5.2",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64"
)

$ErrorActionPreference = "Stop"

function Get-DefaultVtkRoot([string]$ConfigurationName, [bool]$InstallTree) {
    $suffix = if ($ConfigurationName -eq "Debug") { "debug" } else { "release" }
    $root = if ($InstallTree) { "opt" } else { "build" }
    return Join-Path $HOME "$root\vtk-9.5.2-qt611-$suffix"
}

function Get-LegacyReleaseVtkRoot([bool]$InstallTree) {
    $root = if ($InstallTree) { "opt" } else { "build" }
    return Join-Path $HOME "$root\vtk-9.5.2-qt611"
}

function Move-LegacyReleaseTree([string]$LegacyPath, [string]$TargetPath) {
    if ((Test-Path $LegacyPath) -and (Test-Path $TargetPath)) {
        throw "Both the legacy release VTK path '$LegacyPath' and the new release path '$TargetPath' exist. Remove or rename one of them before continuing."
    }
    if (Test-Path $LegacyPath) {
        Move-Item -LiteralPath $LegacyPath -Destination $TargetPath
        Write-Host "Renamed legacy release VTK path '$LegacyPath' -> '$TargetPath'."
    }
}

$sourceDirProvided = -not [string]::IsNullOrWhiteSpace($SourceDir)
$buildDirProvided = -not [string]::IsNullOrWhiteSpace($BuildDir)
$installDirProvided = -not [string]::IsNullOrWhiteSpace($InstallDir)

if (-not $sourceDirProvided) {
    $SourceDir = Join-Path $HOME "src\VTK"
}
if (-not $buildDirProvided) {
    $BuildDir = Get-DefaultVtkRoot $Configuration $false
}
if (-not $installDirProvided) {
    $InstallDir = Get-DefaultVtkRoot $Configuration $true
}

if ($Configuration -eq "Release") {
    if (-not $buildDirProvided) {
        Move-LegacyReleaseTree (Get-LegacyReleaseVtkRoot $false) $BuildDir
    }
    if (-not $installDirProvided) {
        Move-LegacyReleaseTree (Get-LegacyReleaseVtkRoot $true) $InstallDir
    }
}

$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"
$qtCmakeDir = Join-Path $QtRoot "lib\cmake\Qt6"

if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH."
}

if (!(Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}

if (!(Test-Path $cmake)) {
    throw "CMake not found at '$cmake'."
}

if (!(Test-Path $qtCmakeDir)) {
    throw "Qt6Config.cmake not found under '$qtCmakeDir'."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SourceDir), (Split-Path -Parent $BuildDir), (Split-Path -Parent $InstallDir) | Out-Null

if (Test-Path (Join-Path $SourceDir ".git")) {
    & git -C $SourceDir remote update --prune
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to update existing VTK checkout."
    }

    & git -C $SourceDir fetch --tags --force
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to fetch VTK tags."
    }

    & git -C $SourceDir checkout $VtkRef
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout '$VtkRef' in '$SourceDir'."
    }
} elseif (!(Test-Path $SourceDir)) {
    & git clone https://github.com/Kitware/VTK.git $SourceDir
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone VTK."
    }

    & git -C $SourceDir fetch --tags --force
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to fetch VTK tags."
    }

    & git -C $SourceDir checkout $VtkRef
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout '$VtkRef' in '$SourceDir'."
    }
} elseif (Test-Path (Join-Path $SourceDir "CMakeLists.txt")) {
    Write-Host "Reusing existing non-git VTK source tree at '$SourceDir'."
    Write-Host "Skipping git fetch/checkout because the source directory is not a git clone."
} else {
    throw "Source directory '$SourceDir' already exists but does not look like a VTK source tree. Pass -SourceDir to a valid VTK checkout or remove the conflicting directory."
}

$moduleArgs = @(
    "-DVTK_MODULE_ENABLE_VTK_CommonCore=YES",
    "-DVTK_MODULE_ENABLE_VTK_CommonDataModel=YES",
    "-DVTK_MODULE_ENABLE_VTK_InteractionStyle=YES",
    "-DVTK_MODULE_ENABLE_VTK_GUISupportQt=YES",
    "-DVTK_MODULE_ENABLE_VTK_RenderingCore=YES",
    "-DVTK_MODULE_ENABLE_VTK_RenderingOpenGL2=YES",
    "-DVTK_MODULE_ENABLE_VTK_RenderingVolume=YES",
    "-DVTK_MODULE_ENABLE_VTK_RenderingVolumeOpenGL2=YES"
) -join " "

$configureCommand = @(
    "`"$cmake`"",
    "-S", "`"$($SourceDir -replace '\\', '/')`"",
    "-B", "`"$($BuildDir -replace '\\', '/')`"",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_INSTALL_PREFIX=`"$($InstallDir -replace '\\', '/')`"",
    "-DQt6_DIR=`"$($qtCmakeDir -replace '\\', '/')`"",
    "-DVTK_BUILD_ALL_MODULES=OFF",
    "-DVTK_BUILD_EXAMPLES=OFF",
    "-DVTK_BUILD_TESTING=OFF",
    "-DVTK_ENABLE_WRAPPING=OFF",
    $moduleArgs
) -join " "

$buildCommand = @(
    "`"$cmake`"",
    "--build", "`"$($BuildDir -replace '\\', '/')`"",
    "--config", $Configuration,
    "-j", "8"
) -join " "

$installCommand = @(
    "`"$cmake`"",
    "--install", "`"$($BuildDir -replace '\\', '/')`"",
    "--config", $Configuration
) -join " "

$command = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && $configureCommand && $buildCommand && $installCommand"
cmd.exe /c $command

if ($LASTEXITCODE -ne 0) {
    throw "VTK configure/build/install failed with exit code $LASTEXITCODE."
}

$vtkDir = Join-Path $InstallDir "lib\cmake\vtk-9.5"

Write-Host ""
Write-Host "VTK bootstrap complete."
Write-Host "Configuration: $Configuration"
Write-Host "Source: $SourceDir"
Write-Host "Build: $BuildDir"
Write-Host "Install: $InstallDir"
Write-Host "VTK_DIR=$vtkDir"
Write-Host "This bootstrap only requests the VTK modules nd2-viewer links against, plus their dependency closure."
