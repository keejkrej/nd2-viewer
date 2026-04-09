param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$MakensisArguments
)

$ErrorActionPreference = "Stop"

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

if (-not $MakensisArguments -or $MakensisArguments.Count -eq 0) {
    throw "Expected makensis arguments from CPack."
}

$nsisScriptPath = $MakensisArguments | Where-Object { $_ -like "*.nsi" -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
if (-not $nsisScriptPath) {
    throw "Could not find the generated NSIS script in: $($MakensisArguments -join ' ')"
}

$makensisPath = Resolve-MakensisPath
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

& $makensisPath @MakensisArguments
if ($LASTEXITCODE -ne 0) {
    throw "makensis.exe failed with exit code $LASTEXITCODE."
}
