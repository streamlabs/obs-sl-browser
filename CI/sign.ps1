param(
    [string]$github_workspace,
    [string]$revision
)

Write-Output "Workspace is $github_workspace"
Write-Output "Revision is $revision"

# Define the output file name for the 7z archive
$archiveFileName = "slplugin-$env:SL_OBS_VERSION-$revision.7z"

$tempDir = "archive"

# Extract archive to a temp directory
7z x $archiveFileName -o$tempDir

$signtool = "C:\Program Files (x86)\Microsoft SDKs\ClickOnce\SignTool\signtool.exe"
$cert = "code-signing.p7b"

Get-ChildItem -Path $tempDir -File -Recurse |
  ForEach-Object {
    & $signtool sign /tr http://timestamp.digicert.com /td sha256 /fd sha256 /f $cert $_ | Write-Debug
  }
