# ci/make_signed_zip.ps1
param(
  [string]$Version,
  [string]$Sha
)

$signedArchiveFileName = "slplugin-$Version-$Sha-signed.zip"

if (!(Test-Path "archive")) {
  throw "Expected ./archive folder not found"
}

# Overwrite if exists
if (Test-Path $signedArchiveFileName) { Remove-Item -Force $signedArchiveFileName }

Compress-Archive -Path "archive\*" -DestinationPath $signedArchiveFileName -Force
