<#
.SYNOPSIS
    Clones OBS at the tag named in obs.ver, beside the plugin checkout.

.DESCRIPTION
    Used by both CI workflows. The destination has to be the same relative path in each,
    because a prebuilt tree carries absolute paths in CMakeCache.txt and cmake refuses to
    work with a build directory that has moved.

    This is the same recipe as CI/pipeline.ps1, deliberately not shared with it: that script
    also writes to S3, pushes symbols and needs release secrets, none of which belong in a
    test run. Keeping them apart means a change here cannot break the release path.
#>
[CmdletBinding()]
param(
    [string]$PluginDir = 'obs-sl-browser',
    [string]$Destination = 'obs-studio'
)

$ErrorActionPreference = 'Stop'

$ver = (Get-Content (Join-Path $PluginDir 'obs.ver') -Raw).Trim()
if ($ver -notmatch '^\d+\.\d+\.\d+') { throw "obs.ver gave no usable version: '$ver'" }

git clone --recursive --branch $ver https://github.com/obsproject/obs-studio.git $Destination
if ($LASTEXITCODE -ne 0) { throw "clone failed" }

Push-Location $Destination
try {
    git submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw "submodule update failed" }
    Write-Host "OBS $ver at $(Get-Location)"
}
finally { Pop-Location }
