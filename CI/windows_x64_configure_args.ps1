<#
.SYNOPSIS
    Returns CMake's windows-x64 configure arguments for the installed Visual Studio and SDK.

.DESCRIPTION
    OBS 33 beta requests Visual Studio 2026. The Windows 2022 CI runners have Visual Studio
    2022, so override the preset generator and select an installed Windows SDK when needed.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ObsDir
)

$ErrorActionPreference = 'Stop'

$presetsFile = Join-Path $ObsDir 'CMakePresets.json'
$presets = Get-Content -LiteralPath $presetsFile -Raw | ConvertFrom-Json
$preset = @($presets.configurePresets | Where-Object { $_.name -eq 'windows-x64' })
if ($preset.Count -ne 1) { throw "Expected one windows-x64 preset in '$presetsFile'." }

$configureArgs = @('--preset', 'windows-x64')
$generator = $preset[0].generator
$cmakeHelp = (& cmake --help) -join "`n"
if ($LASTEXITCODE -ne 0) { throw 'cmake --help failed.' }
$available = $generator -and $cmakeHelp -match "(?m)^\s*\*?\s*$([regex]::Escape($generator))\s*="

$vswhere = 'C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
if ($generator -match '^Visual Studio (\d+) ') {
    $major = [int]$Matches[1]
    $range = '[{0}.0,{1}.0)' -f $major, ($major + 1)
    $installed = if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        & $vswhere -products '*' -version $range -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    $available = $available -and [bool]$installed
}

if (-not $available) {
    $fallback = 'Visual Studio 17 2022'
    $vs2022 = if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        & $vswhere -products '*' -version '[17.0,18.0)' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    }
    if (-not $vs2022 -or $cmakeHelp -notmatch "(?m)^\s*\*?\s*$([regex]::Escape($fallback))\s*=") {
        throw "The OBS preset uses '$generator', which is unavailable, and Visual Studio 2022 with C++ tools was not found."
    }

    $sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
    $newestSdk = Get-ChildItem -LiteralPath $sdkRoot -Directory |
        Select-Object -ExpandProperty Name |
        Where-Object { $_ -match '^10\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_ } | Select-Object -Last 1
    if (-not $newestSdk) { throw "No Windows 10/11 SDK found under '$sdkRoot'." }

    Write-Host "Using $fallback and Windows SDK $newestSdk instead of $generator"
    $configureArgs += @('-G', $fallback, '-A', "x64,version=$newestSdk")
}

$configureArgs
