# Shared vcpkg root resolution for Windows scripts (dot-source from scripts\*.ps1).

function Test-VcpkgRoot([string]$Root) {
    return Test-Path (Join-Path $Root "scripts\buildsystems\vcpkg.cmake")
}

function Resolve-VcpkgRoot {
    param([string]$Explicit)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        $r = $Explicit.TrimEnd('\', '/')
        if (-not (Test-VcpkgRoot $r)) {
            throw "Invalid -VcpkgRoot: '$r' (expected scripts\buildsystems\vcpkg.cmake)."
        }
        return $r
    }
    if ($env:VCPKG_ROOT) {
        $r = $env:VCPKG_ROOT.TrimEnd('\', '/')
        if (Test-VcpkgRoot $r) {
            return $r
        }
    }
    $scoopCandidate = Join-Path $env:USERPROFILE "scoop\apps\vcpkg\current"
    if (Test-VcpkgRoot $scoopCandidate) {
        return $scoopCandidate
    }
    $whereLines = @(& where.exe vcpkg 2>$null)
    foreach ($line in $whereLines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $dir = Split-Path -Parent $line
        if (Test-VcpkgRoot $dir) {
            return $dir
        }
    }
    throw "Could not find vcpkg (need scripts\buildsystems\vcpkg.cmake). Install vcpkg (e.g. scoop install vcpkg), add vcpkg to PATH, or set VCPKG_ROOT."
}
