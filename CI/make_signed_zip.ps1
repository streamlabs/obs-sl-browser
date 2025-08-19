param(
    [string]$Version,
    [string]$Sha
)

cd archive

$signedArchiveFileName = "slplugin-$Version-$Sha-signed.zip"

7z a "../$signedArchiveFileName" ./*

if ($LASTEXITCODE -ne 0) {
    throw "7z failed with exit code $LASTEXITCODE"
}

cd ..