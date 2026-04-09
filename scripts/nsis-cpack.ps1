# CPack NSIS driver: normalizes CPack/makensis arguments (mirrors former cpack-nsis-wrapper.cpp),
# patches the generated NSIS script for per-user install, then runs makensis.
param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CpackArgs
)

$ErrorActionPreference = "Stop"

function Convert-CpackArg {
    param([string]$Value)
    if ($null -eq $Value) { return '' }
    if ($Value -eq '/--powershell-script') { return '--powershell-script' }
    if ($Value.Length -ge 4 -and $Value[0] -eq '/' -and [char]::IsLetter($Value[1]) -and $Value[2] -eq ':' -and
        ($Value[3] -eq '/' -or $Value[3] -eq '\')) {
        return $Value.Substring(1)
    }
    return $Value
}

$incoming = @()
foreach ($a in $CpackArgs) {
    $incoming += (Convert-CpackArg $a)
}

function Resolve-MakensisPath {
    $explicitPath = $env:ND2_VIEWER_MAKENSIS_EXE
    if ($explicitPath) {
        if (Test-Path -LiteralPath $explicitPath) {
            return (Resolve-Path -LiteralPath $explicitPath).Path
        }
        throw "ND2_VIEWER_MAKENSIS_EXE points to '$explicitPath', but that path does not exist."
    }

    $command = Get-Command "makensis.exe" -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    throw "makensis.exe was not found and ND2_VIEWER_MAKENSIS_EXE was not set."
}

$makensisPath = Resolve-MakensisPath

if ($incoming.Count -eq 1 -and $incoming[0] -eq '/VERSION') {
    & $makensisPath @incoming
    exit $LASTEXITCODE
}

function Require-Count {
    param(
        [string]$Name,
        [int]$Actual,
        [int]$Expected
    )
    if ($Actual -ne $Expected) {
        throw "Expected $Expected occurrence(s) for '$Name', found $Actual."
    }
}

function Replace-Exact {
    param(
        [string]$Content,
        [string]$Search,
        [string]$Replacement,
        [int]$ExpectedCount
    )
    $actualCount = ([regex]::Matches($Content, [regex]::Escape($Search))).Count
    Require-Count -Name $Search -Actual $actualCount -Expected $ExpectedCount
    return $Content.Replace($Search, $Replacement)
}

function Replace-Regex {
    param(
        [string]$Content,
        [string]$Pattern,
        [string]$Replacement,
        [int]$ExpectedCount
    )
    $actualCount = ([regex]::Matches($Content, $Pattern)).Count
    Require-Count -Name $Pattern -Actual $actualCount -Expected $ExpectedCount
    return [regex]::Replace($Content, $Pattern, $Replacement)
}

if (-not $incoming -or $incoming.Count -eq 0) {
    throw "Expected makensis arguments from CPack."
}

$nsisScriptPath = $incoming | Where-Object { $_ -like "*.nsi" -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $nsisScriptPath) {
    throw "Could not find the generated NSIS script in: $($incoming -join ' ')"
}

$content = Get-Content -LiteralPath $nsisScriptPath -Raw

$installDirMatch = [regex]::Match($content, 'InstallDir "([^"]+)"')
if (-not $installDirMatch.Success) {
    throw "Could not determine the generated default install directory from '$nsisScriptPath'."
}

$installDir = $installDirMatch.Groups[1].Value
$installDirReplacement = $installDir.Replace('$', '$$')

$content = Replace-Exact -Content $content `
    -Search 'RequestExecutionLevel admin' `
    -Replacement 'RequestExecutionLevel user' `
    -ExpectedCount 1

$content = Replace-Exact -Content $content `
    -Search 'SetShellVarContext all' `
    -Replacement 'SetShellVarContext current' `
    -ExpectedCount 4

$content = Replace-Exact -Content $content `
    -Search 'StrCpy $SV_ALLUSERS "AllUsers"' `
    -Replacement 'StrCpy $SV_ALLUSERS "JustMe"' `
    -ExpectedCount 3

$content = Replace-Regex -Content $content `
    -Pattern 'StrCpy \$INSTDIR "\$DOCUMENTS\\[^"]+"' `
    -Replacement ('StrCpy $$INSTDIR "{0}"' -f $installDirReplacement) `
    -ExpectedCount 1

$content = Replace-Regex -Content $content `
    -Pattern 'HKLM "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\' `
    -Replacement 'SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\' `
    -ExpectedCount 5

[System.IO.File]::WriteAllText($nsisScriptPath, $content, [System.Text.UTF8Encoding]::new($false))

& $makensisPath @incoming
if ($LASTEXITCODE -ne 0) {
    throw "makensis.exe failed with exit code $LASTEXITCODE."
}
