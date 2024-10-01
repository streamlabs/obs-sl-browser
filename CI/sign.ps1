param(
    [string]$github_workspace,
    [string]$revision
)

Write-Output "Workspace is $github_workspace"
Write-Output "Revision is $revision"

# Define the output file name for the 7z archive
$archiveFileName = "slplugin-$env:SL_OBS_VERSION-$revision.7z"

# Extract archive to a temp directory
7z x $archiveFileName -oarchive

$signtool = "C:\Program Files (x86)\Microsoft SDKs\ClickOnce\SignTool\signtool.exe"
$cert = $env:CODE_SIGNING_CERTIFICATE
$certFile = "sl-code-signing.pfx"
$certPass = $env:CODE_SIGNING_PASSWORD
$signExtensions = ".exe",".dll"
$keepExtensions = ".exe",".dll",".png"

# Download and decode the certificate
echo $cert > "sl-code-signing.b64"
certutil -decode "sl-code-signing.b64" $certFile

$signedArchiveFileName = "slplugin-$env:SL_OBS_VERSION-$revision-signed.zip"

Get-ChildItem -Path "archive" -File -Recurse |
  Where-Object { $signExtensions.Contains($_.Extension) } |
  ForEach-Object {
    Write-Host "NSIS compilation failed"
    # $fullName = $_.FullName
    # & $signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /f $certFile /p $certPass "$fullName"
  }

# Remove the certificate before any file operations are performed
Remove-Item -Path "sl-code-signing.b64"
Remove-Item -Path $certFile
  
# Delete all files in archive that are not keepExtensions
Get-ChildItem -Path "archive" -File -Recurse |
  Where-Object { -not $keepExtensions.Contains($_.Extension) } |
  Remove-Item -Force
  
# Make an installer out of those contents

# Move to nsis folder
Push-Location "${github_workspace}/nsis"

$installerFileName = "slplugin-$env:SL_OBS_VERSION-$revision-signed.exe"
$nsisCommand = "makensis -DPACKAGE_DIR=`"${github_workspace}/archive/RelWithDebInfo`" -DOUTPUT_NAME=`"$installerFileName`" package.nsi"
Invoke-Expression $nsisCommand

if ($LASTEXITCODE -ne 0) {
	Pop-Location
	Write-Error "NSIS compilation failed"
	exit $LASTEXITCODE
}
	
# Download and decode the certificate
echo $cert > "sl-code-signing.b64"
certutil -decode "sl-code-signing.b64" $certFile

# Sign installer
& $signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /f $certFile /p $certPass "$installerFileName"

# Remove the certificate before any more file operations are performed
Remove-Item -Path "sl-code-signing.b64"
Remove-Item -Path $certFile

# Move the signed installer to the specified workspace
Move-Item -Path $installerFileName -Destination "${github_workspace}\"

# Check if the last operation (Move-Item) was successful
if (-not $?) {
	Write-Error "Failed to move installer file"
	exit 1
}

# Return from nsis folder
Pop-Location

# Move to the RelWithDebInfo directory to zip its contents directly
cd archive/RelWithDebInfo

# Create a zip file with the contents of RelWithDebInfo at the top level
7z a "../$signedArchiveFileName" .\*

# Move the signed archive to the specified workspace
Move-Item -Path "..\$signedArchiveFileName" -Destination "${github_workspace}\"