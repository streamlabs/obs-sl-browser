<#
.SYNOPSIS
    Copies the plugin working copy into obs-studio\plugins and registers it with CMake.

.DESCRIPTION
    The plugin has to be built from inside the OBS tree: its sources include
    ..\obs-browser\panel\browser-panel-internal.hpp from OBS's own bundled browser plugin,
    which only resolves from plugins\obs-sl-browser.

    Refuses to overwrite an existing copy rather than deleting one. On CI the destination is
    always absent - a restored prebuilt tree is built with no plugin in it at all - so a
    collision means something is wrong and is worth stopping for.
#>
[CmdletBinding()]
param(
    [string]$PluginDir = 'obs-sl-browser',
    [string]$ObsDir = 'obs-studio'
)

$ErrorActionPreference = 'Stop'

$listFile = Join-Path $ObsDir 'plugins\CMakeLists.txt'
$lines = Get-Content $listFile

if (-not ($lines -match 'add_subdirectory\(obs-sl-browser\)')) {
    Set-Content $listFile -Value ($lines[0], 'add_subdirectory(obs-sl-browser)', $lines[1..($lines.Length - 1)])
    Write-Host "registered in plugins/CMakeLists.txt"
}
else {
    Write-Host "already registered in plugins/CMakeLists.txt"
}

$dest = Join-Path $ObsDir 'plugins\obs-sl-browser'
if (Test-Path $dest) { throw "$dest already exists - refusing to overwrite it" }

Copy-Item -Path $PluginDir -Destination $dest -Recurse
Write-Host "grafted $PluginDir into $dest"
