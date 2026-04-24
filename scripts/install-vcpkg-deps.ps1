param(
    [string]$VcpkgRoot = "",
    [string]$VcpkgTriplet = "x64-windows"
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot "vcpkg-common.ps1")

$resolvedVcpkgRoot = Resolve-VcpkgRoot -Explicit $VcpkgRoot
$vcpkgExe = Join-Path $resolvedVcpkgRoot "vcpkg.exe"
if (!(Test-Path $vcpkgExe)) {
    throw "vcpkg.exe not found at '$vcpkgExe'."
}

Write-Host "vcpkg: installing manifest dependencies (Qt, VTK, ITK, libczi, ...) triplet=$VcpkgTriplet"
Write-Host "vcpkg root: $resolvedVcpkgRoot"

Push-Location $repoRoot
try {
    & $vcpkgExe install --triplet $VcpkgTriplet --vcpkg-root $resolvedVcpkgRoot
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg install failed with exit code $LASTEXITCODE."
    }
} finally {
    Pop-Location
}

Write-Host "vcpkg install finished."
