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
$cert = "sl-code-signing.pfx"
$certPass = $env:CODE_SIGNING_PASSWORD
$signExtensions = ".exe",".dll"

Get-ChildItem -Path "archive" -File -Recurse |
  Where-Object { $signExtensions.Contains($_.Extension) } |
  ForEach-Object {
    $fullName = $_.FullName
    & $signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /f $cert /p $certPass "$fullName"
  }

7z a $archiveFileName archive\RelWithDebInfo

Move-Item -Path $archiveFileName -Destination "${github_workspace}\"
