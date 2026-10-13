param(
    [string]$Version,
    [string]$Sha
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$signedArchiveFileName = & (Join-Path $PSScriptRoot 'package_file_name.ps1') -Version $Version -Sha $Sha -Extension zip

Push-Location (Join-Path $repoRoot 'archive')
try {
    7z a (Join-Path $repoRoot $signedArchiveFileName) .\*

    if ($LASTEXITCODE -ne 0) {
        throw "7z failed with exit code $LASTEXITCODE"
    }
}
finally { Pop-Location }
