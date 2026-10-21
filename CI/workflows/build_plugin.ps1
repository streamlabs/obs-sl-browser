<#
.SYNOPSIS
    Configures the grafted OBS tree and builds it. For the e2e job.

.PARAMETER PluginOnly
    Build the three plugin targets and their dependencies. OBS 33 also regenerates its core
    module list and relinks obs.dll. Without this switch the whole of OBS is built, which is
    what happens when the archive was missing.
#>
[CmdletBinding()]
param(
    [string]$PluginDir = 'obs-sl-browser',
    [string]$ObsDir = 'obs-studio',
    [switch]$PluginOnly,
    [string]$LogFile = 'build.log'
)

$ErrorActionPreference = 'Stop'

$ver = (Get-Content (Join-Path $PluginDir 'obs.ver') -Raw).Trim()
if ($ver -notmatch '^\d+\.\d+\.\d+') { throw "obs.ver gave no usable version: '$ver'" }

Push-Location $ObsDir
try {
    $cfg = @(& (Join-Path $PSScriptRoot '..\windows_x64_configure_args.ps1') -ObsDir .)
    $cfg += @('-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF', "-DOBS_VERSION_OVERRIDE=$ver")
    # Passed on the command line rather than patched into CMakePresets.json so that a restored
    # tree and a from-scratch tree configure identically. The override is quoted because
    # PowerShell does not expand variables inside an unquoted token that starts with a dash -
    # unquoted, cmake gets the literal '$ver' and versionconfig.cmake aborts.
    cmake @cfg
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }

    if ($PluginOnly) {
        cmake --build build_x64 --config RelWithDebInfo `
            --target sl-browser-plugin sl-browser sl-browser-page | Tee-Object -FilePath $LogFile
    }
    else {
        cmake --build --preset windows-x64 | Tee-Object -FilePath $LogFile
    }
    if ($LASTEXITCODE -ne 0) { throw "build failed" }
}
finally { Pop-Location }
