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

# Add information to the revision library
$slRevision = 0

try {
    # Download data for revisions
    $urlJsonObsVersions = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"
    $filepathJsonPublish = ".\meta_publish.json"
    Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish
    $jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
    
    $slRevision = $jsonContent.next_rev
    Write-Output "Streamlabs revision is $slRevision"
    
    # Attempt to download existing revision builds JSON
    $urlJsonRevisionBuilds = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/revision_builds.json"
    $filepathJsonRevisionBuilds = ".\revision_builds.json"
    
    try {
        Invoke-WebRequest -Uri $urlJsonRevisionBuilds -OutFile $filepathJsonRevisionBuilds
    }
    catch {
        # If download fails, assume file does not exist and create a new JSON array
        Set-Content -Path $filepathJsonRevisionBuilds -Value "[]"
    }

    # Load or initialize the revision builds JSON
    $revisionBuilds = Get-Content -Path $filepathJsonRevisionBuilds -Raw | ConvertFrom-Json
    
    # Prepare the new entry
    $newEntry = @{
        "rev" = $slRevision
        "date" = (Get-Date -UFormat %s)
        "gitrev" = $revision
    }

    # Check if the entry already exists (based on rev and gitrev)
    $exists = $false
    foreach ($entry in $revisionBuilds) {
        if ($entry.rev -eq $newEntry.rev -and $entry.gitrev -eq $newEntry.gitrev) {
            $exists = $true
            break
        }
    }
    
    # If the entry does not exist, add it
    if (-not $exists) {
        $revisionBuilds += $newEntry
    }
    
    # Save the updated JSON back to the file
    $revisionBuilds | ConvertTo-Json -Depth 100 | Set-Content -Path $filepathJsonRevisionBuilds
    
    Write-Output "Revision builds json formed. Uploading back to AWS."
	
	# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
	$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
	$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
	$Env:AWS_DEFAULT_REGION = "us-west-2"
	
	try {
		aws s3 cp $jsonFilePath https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
		
		if ($LASTEXITCODE -ne 0) {
			throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
		}
}
catch {
    throw "Error: An error occurred during revision updating. Details: $($_.Exception.Message)"
}