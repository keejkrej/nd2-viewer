param(
    [string]$VtkRef = "v9.5.2",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = "",
    [string]$QtRoot = "C:\Qt\6.11.0\msvc2022_64"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $HOME "src\VTK"
}
if ([string]::IsNullOrWhiteSpace($BuildDir)) {
    $BuildDir = Join-Path $HOME "build\vtk-9.5.2-qt611"
}
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $HOME "opt\vtk-9.5.2-qt611"
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
    "-DCMAKE_BUILD_TYPE=Release",
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
    "--config", "Release",
    "-j", "8"
) -join " "

$installCommand = @(
    "`"$cmake`"",
    "--install", "`"$($BuildDir -replace '\\', '/')`"",
    "--config", "Release"
) -join " "

$command = "`"$vsDevCmd`" -arch=x64 -host_arch=x64 && $configureCommand && $buildCommand && $installCommand"
cmd.exe /c $command

if ($LASTEXITCODE -ne 0) {
    throw "VTK configure/build/install failed with exit code $LASTEXITCODE."
}

$vtkDir = Join-Path $InstallDir "lib\cmake\vtk-9.5"

Write-Host ""
Write-Host "VTK bootstrap complete."
Write-Host "Source: $SourceDir"
Write-Host "Build: $BuildDir"
Write-Host "Install: $InstallDir"
Write-Host "VTK_DIR=$vtkDir"
Write-Host "This bootstrap only requests the VTK modules nd2-viewer links against, plus their dependency closure."
