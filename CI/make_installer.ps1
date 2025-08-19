param(
    [string]$Version,
    [string]$Sha
)

Push-Location nsis

$installerFileName = "slplugin-$Version-$Sha-signed.exe"

makensis -DPACKAGE_DIR="../archive" -DOUTPUT_NAME="$installerFileName" package.nsi

if ($LASTEXITCODE -ne 0) {
    throw "NSIS compilation failed with exit code $LASTEXITCODE"
}

Move-Item -Path $installerFileName -Destination ../

Pop-Location