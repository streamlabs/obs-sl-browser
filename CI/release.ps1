# URL to the JSON data
$jsonUrl = "https://slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Boolean flag, initially set to false
$allBranchesReady = $false

# Function to check and download files
function CheckAndDownloadFile($url, $folder, $branchName) {
    $filePath = Join-Path $folder (Split-Path -Leaf $url)
    try {
        # Overwrite the file if it already exists
        Invoke-WebRequest -Uri $url -OutFile $filePath

        # Unzip and verify the number of files
        $extractedPath = Join-Path $folder "$branchName"
        if (Test-Path $extractedPath) { Remove-Item -Path $extractedPath -Recurse -Force }
        Expand-Archive -Path $filePath -DestinationPath $extractedPath -PassThru
        $files = Get-ChildItem -Path $extractedPath -Recurse
        if ($files.Count -le 2) { throw "Insufficient files in the archive." }
    } catch {
        Write-Host "$branchName failed: '$_', URL is $url" -ForegroundColor Yellow
        return $false
    }
    return $true
}

# Variable to track successful branches
$successfulBranches = 0

# Iterate through each branch
foreach ($branchName in $branchNames) {
    # Fetch head commit SHA for the branch
    $commitSha = git rev-parse $branchName

    # URL for the zip file
    $zipUrl = "https://slobs-cdn.streamlabs.com/obsplugin/intermediary_packages/slplugin-$branchName-$commitSha-signed.zip"

    # Directory for downloading and unzipping
    $downloadDir = ".\.ReleaseTemp\$branchName\"
    if (-not (Test-Path $downloadDir)) { New-Item -ItemType Directory -Path $downloadDir -Force }

    # Check and download file
    $result = CheckAndDownloadFile $zipUrl $downloadDir $branchName
    if ($result) { 
		$successfulBranches++
        Write-Host "$branchName is ready."
	}
}

# Check if all branches are ready
$allBranchesReady = $successfulBranches -eq $branchNames.Count -and $branchNames.Count -gt 0
if ($allBranchesReady) {
    Write-Host "All branches ready." -ForegroundColor Green
} else {
    Write-Host "Not all branches are ready." -ForegroundColor Red
}
