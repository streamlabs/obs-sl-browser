<#
.SYNOPSIS
    Prints the tail of the most recent OBS log. Diagnostics for a failed e2e run.
#>
[CmdletBinding()]
param(
    [string]$ObsDir = 'obs-studio',
    [string]$Config = 'RelWithDebInfo',
    [int]$Lines = 120
)

$logs = Join-Path $ObsDir "build_x64\rundir\$Config\config\obs-studio\logs"

$log = Get-ChildItem $logs -Filter *.txt -ErrorAction SilentlyContinue |
    Sort-Object LastWriteTime | Select-Object -Last 1

if (-not $log) {
    Write-Host '::warning::OBS produced no log.'
    exit 0
}

Write-Host "---- $($log.Name) ----"
Get-Content $log.FullName -Tail $Lines
