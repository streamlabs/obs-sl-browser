<#
.SYNOPSIS
    Installs the built tree into OBS's rundir, ready for the e2e suites to launch it.

.DESCRIPTION
    The explicit prefix only keeps the default C:\Program Files out of it; rundir is populated
    from an absolute OBS_OUTPUT_DIR either way. Same step as CI\dev_build.ps1 -Run.
#>
[CmdletBinding()]
param(
    [string]$ObsDir = 'obs-studio',
    [string]$Config = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

Push-Location $ObsDir
try {
    cmake --install build_x64 --config $Config --prefix "$pwd\build_x64\install"
    if ($LASTEXITCODE -ne 0) { throw "install failed" }

    $exe = "build_x64\rundir\$Config\bin\64bit\obs64.exe"
    if (-not (Test-Path $exe)) { throw "no obs64.exe in the rundir after install" }
    Write-Host "rundir ready: $(Resolve-Path $exe)"
}
finally { Pop-Location }
