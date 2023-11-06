param(
    [string]$github_workspace,
    [string]$downloadURL
)

# We start inside obs-sl-browser folder, move up to make room for cloning OBS and moving obs-sl-browser into it
cd ..\

# Download the zip at downloadURL, make a folder called 'obs', move the zip in there, unzip it in there and then remove the zip file
$obsFolder = "obs"
$zipFile = "obs-studio.zip"

# Create the folder if it doesn't exist
if (-not (Test-Path -Path $obsFolder)) {
    New-Item -ItemType Directory -Path $obsFolder
}

# Change directory to 'obs'
cd $obsFolder

# Use Invoke-WebRequest to download the file
Write-Output "Downloading ${downloadURL}"
Invoke-WebRequest -Uri $downloadURL -OutFile $zipFile

# Unzip the file
Expand-Archive -LiteralPath $zipFile -DestinationPath "."

# Remove the zip file after extraction
Remove-Item -Path $zipFile

# Change directory back to the original script location
cd ..\

# Clone symbol scripts
git clone --recursive --branch "no-http-source" https://github.com/stream-labs/symsrv-scripts.git

# Run symbols
cd symsrv-scripts
.\main.ps1 -localSourceDir "${github_workspace}\..\${obsFolder}"

# Cleanup
cd ..\
Remove-Item -Path "symsrv-scripts" -Recurse -Force
Remove-Item -Path "${github_workspace}\..\${obsFolder}" -Recurse -Force


if ($LastExitCode -ne 0) {
    throw "Symbol processing script exited with error code $LastExitCode"
}
