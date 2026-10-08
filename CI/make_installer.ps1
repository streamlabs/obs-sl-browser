param(
    [string]$Version,
    [string]$Sha
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$packageDir = Join-Path $repoRoot 'archive'
$installerFileName = & (Join-Path $PSScriptRoot 'package_file_name.ps1') -Version $Version -Sha $Sha -Extension exe
$obsVersion = (Get-Content -LiteralPath (Join-Path $repoRoot 'obs.ver') -Raw).Trim()
if ($obsVersion -notmatch '^(\d+)\.') { throw "Invalid OBS version '$obsVersion' in obs.ver." }
$obsMajor = [int]$Matches[1]

foreach ($name in @('sl-browser.exe', 'sl-browser-page.exe', 'sl-browser-plugin.dll', 'streamlabs-app-icon.png')) {
    if (-not (Test-Path -LiteralPath (Join-Path $packageDir $name) -PathType Leaf)) {
        throw "Package is missing '$name' in '$packageDir'."
    }
}

Push-Location (Join-Path $repoRoot 'nsis')
try {
    $nsisArgs = @('-DPACKAGE_DIR=../archive', "-DOUTPUT_NAME=$installerFileName")
    if ($obsMajor -ge 33) { $nsisArgs += '-DOBS_CORE_LAYOUT' }
    makensis @nsisArgs package.nsi
    if ($LASTEXITCODE -ne 0) {
        throw "NSIS compilation failed with exit code $LASTEXITCODE"
    }

    Move-Item -LiteralPath $installerFileName -Destination $repoRoot
}
finally { Pop-Location }
