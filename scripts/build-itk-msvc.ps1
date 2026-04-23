param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration,
    [string]$ItkRef = "v5.4.4",
    [string]$SourceDir = "",
    [string]$BuildDir = "",
    [string]$InstallDir = ""
)

$ErrorActionPreference = "Stop"

function Get-DefaultItkRoot([string]$ConfigurationName, [bool]$InstallTree) {
    $suffix = if ($ConfigurationName -eq "Debug") { "debug" } else { "release" }
    $root = if ($InstallTree) { "opt" } else { "build" }
    return Join-Path $HOME "$root\itk-5.4.4-$suffix"
}

function Find-ItkConfigDir([string]$Root) {
    $exact = Join-Path $Root "lib\cmake\ITK-5.4"
    if ((Test-Path (Join-Path $exact "ITKConfig.cmake")) -or (Test-Path (Join-Path $exact "itk-config.cmake"))) {
        return $exact
    }

    $candidate = Get-ChildItem -LiteralPath (Join-Path $Root "lib\cmake") -Directory -Filter "ITK-*" -ErrorAction SilentlyContinue |
        Where-Object { (Test-Path (Join-Path $_.FullName "ITKConfig.cmake")) -or (Test-Path (Join-Path $_.FullName "itk-config.cmake")) } |
        Select-Object -First 1
    if ($candidate) {
        return $candidate.FullName
    }

    return $exact
}

$sourceDirProvided = -not [string]::IsNullOrWhiteSpace($SourceDir)
$buildDirProvided = -not [string]::IsNullOrWhiteSpace($BuildDir)
$installDirProvided = -not [string]::IsNullOrWhiteSpace($InstallDir)

if (-not $sourceDirProvided) {
    $SourceDir = Join-Path $HOME "src\ITK"
}
if (-not $buildDirProvided) {
    $BuildDir = Get-DefaultItkRoot $Configuration $false
}
if (-not $installDirProvided) {
    $InstallDir = Get-DefaultItkRoot $Configuration $true
}

$vsDevCmd = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat"
$cmake = "C:\Qt\Tools\CMake_64\bin\cmake.exe"

if (!(Get-Command git -ErrorAction SilentlyContinue)) {
    throw "git was not found on PATH."
}

if (!(Test-Path $vsDevCmd)) {
    throw "VsDevCmd.bat not found at '$vsDevCmd'."
}

if (!(Test-Path $cmake)) {
    throw "CMake not found at '$cmake'."
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $SourceDir), (Split-Path -Parent $BuildDir), (Split-Path -Parent $InstallDir) | Out-Null

if (Test-Path (Join-Path $SourceDir ".git")) {
    & git -C $SourceDir remote update --prune
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to update existing ITK checkout."
    }

    & git -C $SourceDir fetch --tags --force
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to fetch ITK tags."
    }

    & git -C $SourceDir checkout $ItkRef
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout '$ItkRef' in '$SourceDir'."
    }
} elseif (!(Test-Path $SourceDir)) {
    & git clone https://github.com/InsightSoftwareConsortium/ITK.git $SourceDir
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone ITK."
    }

    & git -C $SourceDir fetch --tags --force
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to fetch ITK tags."
    }

    & git -C $SourceDir checkout $ItkRef
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to checkout '$ItkRef' in '$SourceDir'."
    }
} elseif (Test-Path (Join-Path $SourceDir "CMakeLists.txt")) {
    Write-Host "Reusing existing non-git ITK source tree at '$SourceDir'."
    Write-Host "Skipping git fetch/checkout because the source directory is not a git clone."
} else {
    throw "Source directory '$SourceDir' already exists but does not look like an ITK source tree. Pass -SourceDir to a valid ITK checkout or remove the conflicting directory."
}

$moduleArgs = @(
    "-DITK_BUILD_DEFAULT_MODULES=OFF",
    "-DModule_ITKCommon=ON",
    "-DModule_ITKDeconvolution=ON"
) -join " "

$configureCommand = @(
    "`"$cmake`"",
    "-S", "`"$($SourceDir -replace '\\', '/')`"",
    "-B", "`"$($BuildDir -replace '\\', '/')`"",
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_INSTALL_PREFIX=`"$($InstallDir -replace '\\', '/')`"",
    "-DBUILD_TESTING=OFF",
    "-DBUILD_EXAMPLES=OFF",
    "-DITK_BUILD_DOCUMENTATION=OFF",
    "-DITK_BUILD_DOXYGEN=OFF",
    "-DITK_WRAP_PYTHON=OFF",
    "-DITK_WRAP_DEFAULT=OFF",
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
    throw "ITK configure/build/install failed with exit code $LASTEXITCODE."
}

$itkDir = Find-ItkConfigDir $InstallDir

Write-Host ""
Write-Host "ITK bootstrap complete."
Write-Host "Configuration: $Configuration"
Write-Host "Source: $SourceDir"
Write-Host "Build: $BuildDir"
Write-Host "Install: $InstallDir"
Write-Host "ITK_DIR=$itkDir"
Write-Host "This bootstrap only requests the ITK modules nd2-viewer links against, plus their dependency closure."
