param(
    [string]$github_workspace,
    [string]$revision
)

$pathToFiles = "${github_workspace}\..\${revision}\build64\plugins\obs-sl-browser\RelWithDebInfo"

Push-Location .\nsis

$installerFileName = "slplugin-$env:SL_OBS_VERSION-$revision.exe"
$nsisCommand = "makensis -DPACKAGE_DIR=`"$pathToFiles`" -DOUTPUT_NAME=`"$installerFileName`" package.nsi"
Invoke-Expression $nsisCommand

if ($LASTEXITCODE -ne 0) {
    Pop-Location
    Write-Error "NSIS compilation failed"
    exit $LASTEXITCODE
}

# Move the installer file to the root of the GitHub workspace
$destinationPath = Join-Path $github_workspace $installerFileName
Move-Item $installerFileName $destinationPath -Force

# Check if the last operation (Move-Item) was successful
if (-not $?) {
    Write-Error "Failed to move installer file"
    exit 1
}

Pop-Location
