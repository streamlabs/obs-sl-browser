<#
.SYNOPSIS
    Returns a Windows-safe installer or zip filename for a Git ref and commit.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Version,
    [Parameter(Mandatory = $true)]
    [string]$Sha,
    [Parameter(Mandatory = $true)]
    [ValidateSet('exe', 'zip')]
    [string]$Extension
)

if ([string]::IsNullOrWhiteSpace($Version)) { throw 'Version cannot be empty.' }
if ($Sha -notmatch '^[0-9a-fA-F]{40}$') { throw "Invalid commit SHA '$Sha'." }

# Branches such as try/obs-33-beta6-local-build must be one filename component.
$safeVersion = $Version -replace '[^A-Za-z0-9._-]', '-'
"slplugin-$safeVersion-$($Sha.ToLowerInvariant())-signed.$Extension"
