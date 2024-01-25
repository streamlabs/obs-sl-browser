# URL to the JSON data
$jsonUrl = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Boolean flag, initially set to false
$allBranchesReady = $false

# Function to check and download files
function CheckAndDownloadZip($url, $folder, $branchName) {
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

# URL's for each branch's installer
$branchInstallerUrls = @{}

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
	
	# Download and check zip file
	$zipResult = CheckAndDownloadZip $zipUrl $downloadDir $branchName

	if ($zipResult){
		# Check the installer by copying it over into /package/
		$installerUrl = "s3://slobs-cdn.streamlabs.com/obsplugin/intermediary_packages/slplugin-$branchName-$commitSha-signed.exe"
		$destination = "s3://slobs-cdn.streamlabs.com/obsplugin/package/slplugin-$branchName-$commitSha-signed.exe"    
		$installerResult = $false
			
		try {
			aws s3 cp $installerUrl $destination --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate" --debug
			
			if ($LASTEXITCODE -ne 0) {
				throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
			}
			
			$installerResult = $true
		} catch {
			Write-Host "Failed to copy or delete the installer file for $branchName"
		}

		# Check if both operations were successful
		if ($installerResult) { 
			$successfulBranches++
			$branchInstallerUrls[$branchName] = "https://slobs-cdn.streamlabs.com/obsplugin/package/slplugin-$branchName-$commitSha-signed.exe"
			Write-Host "$branchName is ready."
		}
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
    $jsonContent.installer = $branchInstallerUrls[$branchName]
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
		
		if ($LASTEXITCODE -ne 0) {
			throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
		}
		
		aws s3 cp $zipFilePath s3://slobs-cdn.streamlabs.com/obsplugin/package/	--acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
        
		if ($LASTEXITCODE -ne 0) {
			throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
		}
		
		cfcli -d streamlabs.com purge $jsonFilePath
		
		if ($LASTEXITCODE -ne 0) {
			throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
		}
		
		cfcli -d streamlabs.com purge $zipFilePath
		
		if ($LASTEXITCODE -ne 0)  {
			throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
		}
	}
	catch {
		Write-Host "Error: $_"
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