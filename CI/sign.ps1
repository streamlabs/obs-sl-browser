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
    $fullName = $_.FullName
    & $signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /f $certFile /p $certPass "$fullName"
  }
  
# Delete all files in archive that are not keepExtensions
Get-ChildItem -Path "archive" -File -Recurse |
  Where-Object { -not $keepExtensions.Contains($_.Extension) } |
  Remove-Item -Force

# Move to the RelWithDebInfo directory to zip its contents directly
cd archive/RelWithDebInfo

# Create a zip file with the contents of RelWithDebInfo at the top level
7z a "../$signedArchiveFileName" .\*

# Move the signed archive to the specified workspace
Move-Item -Path "..\$signedArchiveFileName" -Destination "${github_workspace}\"
