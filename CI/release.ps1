# URL to the JSON data
$jsonUrl = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

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
		Expand-Archive -Path $filePath -DestinationPath $extractedPath
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
	git fetch origin $branchName
	$commitSha = git rev-parse FETCH_HEAD

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
    Write-Host "All branches ready."
} else {
    Write-Host "Not all branches are ready."
	throw "Error: Not all branches are ready."
}


# Function to create JSON file for each branch
function CreateJsonFile($folder, $branchName) {
    $jsonFilePath = Join-Path $folder "$branchName.json"
    $zipFile = Get-ChildItem -Path $folder -Filter "*.zip"
    $branchFolderPath = Join-Path $folder $branchName
    $filesInBranch = Get-ChildItem -Path $branchFolderPath -Recurse

    $jsonContent = @{}
    $jsonContent.package = $zipFile.Name
    $jsonContent.files = $filesInBranch | Where-Object { -not $_.PSIsContainer } | ForEach-Object {
        $subPath = $_.FullName.Substring($_.FullName.IndexOf("$branchName\$branchName") + $branchName.Length * 2 + 2)
        @{
            file = $subPath.Replace('\', '/')
            hash = (Get-FileHash $_.FullName -Algorithm MD5).Hash
        }
    }

    $jsonContent | ConvertTo-Json -Depth 10 | Out-File $jsonFilePath
	
    $zipFilePath = Join-Path $folder $zipFile
	
	Write-Host $jsonFilePath
	Write-Host $zipFilePath
	
	# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
	$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
	$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
	$Env:AWS_DEFAULT_REGION = "us-west-2"
	
	try {
		aws s3 cp $jsonFilePath s3://slobs-cdn.streamlabs.com/obsplugin/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
		aws s3 cp $zipFilePath s3://slobs-cdn.streamlabs.com/obsplugin/package/	--acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
        cfcli -d streamlabs.com purge $jsonFilePath
        cfcli -d streamlabs.com purge $zipFilePath
	}
	catch {
		Write-Host "S3Upload: Error: $_"
		throw
	}
}

if ($allBranchesReady) {
    foreach ($branchName in $branchNames) {
		$downloadDir = ".\.ReleaseTemp\$branchName\"
		Write-Host "Creating json file created for $branchName..."
		CreateJsonFile $downloadDir $branchName
	}
}