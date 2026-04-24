# Shared vcpkg root resolution for Windows scripts (dot-source from scripts\*.ps1).

function Test-VcpkgRoot([string]$Root) {
    return Test-Path (Join-Path $Root "scripts\buildsystems\vcpkg.cmake")
}

function Resolve-PhysicalPath([string]$Path) {
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    $item = Get-Item -LiteralPath $resolved.ProviderPath -Force
    if ($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) {
        $target = $item.Target
        if ($target -is [array]) {
            $target = $target[0]
        }
        if (-not [string]::IsNullOrWhiteSpace($target)) {
            return (Resolve-Path -LiteralPath $target -ErrorAction Stop).ProviderPath.TrimEnd('\', '/')
        }
    }
    return $resolved.ProviderPath.TrimEnd('\', '/')
}

function Resolve-VcpkgRoot {
    param([string]$Explicit)
    if (-not [string]::IsNullOrWhiteSpace($Explicit)) {
        $r = Resolve-PhysicalPath $Explicit.TrimEnd('\', '/')
        if (-not (Test-VcpkgRoot $r)) {
            throw "Invalid -VcpkgRoot: '$r' (expected scripts\buildsystems\vcpkg.cmake)."
        }
        return $r
    }
    if ($env:VCPKG_ROOT) {
        $r = Resolve-PhysicalPath $env:VCPKG_ROOT.TrimEnd('\', '/')
        if (Test-VcpkgRoot $r) {
            return $r
        }
    }
    $homeCandidate = Join-Path $env:USERPROFILE "vcpkg"
    if (Test-VcpkgRoot $homeCandidate) {
        return Resolve-PhysicalPath $homeCandidate
    }
    $whereLines = @(& where.exe vcpkg 2>$null)
    foreach ($line in $whereLines) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $dir = Split-Path -Parent $line
        if (Test-VcpkgRoot $dir) {
            return Resolve-PhysicalPath $dir
        }
    }
    throw "Could not find vcpkg (need scripts\buildsystems\vcpkg.cmake). Clone vcpkg to '$env:USERPROFILE\vcpkg', add vcpkg to PATH, or set VCPKG_ROOT."
}
