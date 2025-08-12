param(
    [string]$github_workspace,
    [string]$revision
)

Write-Output "Workspace is $github_workspace"
Write-Output "Github revision is $revision"

# Get the revision we're using
$slRevision = 0

try {
	# Download data for revisions
	$urlJsonObsVersions = "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

	$filepathJsonPublish = ".\meta_publish.json"
	Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish
	$jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
	
	$slRevision = $jsonContent.next_rev
	Write-Output "Streamlabs revision is $slRevision"
}
catch {
	throw "Error: An error occurred. Details: $($_.Exception.Message)"
}

# Save in the bucket which revision was used
$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
$Env:AWS_DEFAULT_REGION = "us-west-2"

$revisionFilePath = Join-Path -Path $github_workspace -ChildPath "${revision}.json"
$newJsonContent = @{ rev = $slRevision } | ConvertTo-Json
$newJsonContent | Out-File -FilePath $revisionFilePath
Write-Output "New JSON file created at $revisionFilePath with content: $newJsonContent"

aws s3 cp $revisionFilePath s3://slobs-cdn.streamlabs.com/obsplugin/meta_sha/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"

if ($LASTEXITCODE -ne 0) {
	throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
}

# Begin
$env:Protobuf_DIR = "${github_workspace}\..\grpc_dist\cmake"
$env:absl_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\absl"
$env:gRPC_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\grpc"
$env:utf8_range_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\utf8_range"

# Edit the CMAKE with the SL_OBS_VERSION and $revision
# Read the content of CMakeLists.txt into a variable
$cmakeContent = Get-Content -Path .\CMakeLists.txt -Raw

# Replace the placeholders with the actual environment variable value
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE SL_OBS_VERSION=""\)', "target_compile_definitions(sl-browser PRIVATE SL_OBS_VERSION=`"$($env:SL_OBS_VERSION)`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE SL_OBS_VERSION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE SL_OBS_VERSION=`"$($env:SL_OBS_VERSION)`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE GITHUB_REVISION=""\)', "target_compile_definitions(sl-browser PRIVATE GITHUB_REVISION=`"${revision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE GITHUB_REVISION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE GITHUB_REVISION=`"${revision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser PRIVATE SL_REVISION=""\)', "target_compile_definitions(sl-browser PRIVATE SL_REVISION=`"${slRevision}`")"
$cmakeContent = $cmakeContent -replace '#target_compile_definitions\(sl-browser-plugin PRIVATE SL_REVISION=""\)', "target_compile_definitions(sl-browser-plugin PRIVATE SL_REVISION=`"${slRevision}`")"

# Write the updated content back to CMakeLists.txt
Set-Content -Path .\CMakeLists.txt -Value $cmakeContent

# Output the updated content to the console
Write-Output "Updated cmake with SL_OBS_VERSION definition:"
Write-Output $cmakeContent

# We start inside obs-sl-browser folder, move up to make room for cloning OBS and moving obs-sl-browser into it
cd ..\

# Deps
.\obs-sl-browser\ci\install_deps.cmd

# Read the obs.ver file to get the branch name
$branchName = Get-Content -Path ".\obs-sl-browser\obs.ver" -Raw

# Clone obs-studio repository with the branch name
git clone --recursive --branch $branchName https://github.com/obsproject/obs-studio.git

# Rename 'obs-studio' folder to name of git tree so pdb's know which version it was built with
Rename-Item -Path ".\obs-studio" -NewName $revision

# Update submodules in obs-studio
cd $revision
git submodule update --init --recursive

# Add new line to CMakeLists.txt in obs-studio\plugins
$cmakeListsPath = ".\plugins\CMakeLists.txt"
$addSubdirectoryLine = "add_subdirectory(obs-sl-browser)"
Add-Content -Path $cmakeListsPath -Value $addSubdirectoryLine

# Move obs-sl-browser folder into obs-studio\plugins
Copy-Item -Path "..\obs-sl-browser" -Destination ".\plugins\obs-sl-browser" -Recurse

# Build
try {
    .\CI\build-windows.ps1 -ErrorAction Stop
}
catch {
    # Handle the error
    Write-Host "Error: $_"
    exit 1
}
if ($LASTEXITCODE -ne 0) {
    throw "Build failed with exit code ${LastExitCode}"
}

# Clone symbols store scripts
Write-Output "-- Symbols"
cd ..\
git clone --recursive --branch "no-http-source" https://github.com/streamlabs/symsrv-scripts.git

# Run symbols (re-try for 5 minutes)
cd symsrv-scripts
$startTime = Get-Date
$maxDuration = New-TimeSpan -Minutes 5

# Last error code initialized to a non-zero value
$lastExitCode = 1

while ((Get-Date) - $startTime -lt $maxDuration -and $lastExitCode -ne 0) {
    .\main.ps1 -localSourceDir "${github_workspace}\..\${revision}\build64\plugins\obs-sl-browser\RelWithDebInfo"

    # Update the last exit code
    $lastExitCode = $LastExitCode

    # Add a delay between retries
    Start-Sleep -Seconds 10
}

# Check if the last exit code is non-zero and throw the error
if ($lastExitCode -ne 0) {
    throw "Symbol processing script exited with error code $lastExitCode"
}

# Define the output file name for the 7z archive
Write-Output "-- 7z"
$pathToArchive = "${github_workspace}\..\${revision}\build64\plugins\obs-sl-browser\RelWithDebInfo"
Write-Output $pathToArchive

# Check if the path exists
if (Test-Path -Path $pathToArchive) {
    # Create a 7z archive of the $revision folder
    $archiveFileName = "slplugin-$env:SL_OBS_VERSION-$revision.7z"
    7z a $archiveFileName $pathToArchive

    # Output the name of the archive file created
    Write-Output "Archive created: $archiveFileName"
} else {
    # Throw an error if the path does not exist
    throw "Error: The path $pathToArchive does not exist."
}

# Output the name of the archive file created
Write-Output "Archive created: $archiveFileName"

# Move the 7z archive to the $github_workspace directory
Move-Item -Path $archiveFileName -Destination "${github_workspace}\"