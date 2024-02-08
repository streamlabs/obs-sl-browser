# URL to the JSON data for known OBS versions 
$urlJsonObsVersions = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $urlJsonObsVersions
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Boolean flag, initially set to false
$allBranchesReady = $false

# Function to check and download files
# The purpose of the zip is to easily have the files for each seperate version so that json metadata can be made
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
		$destinationUrl = "https://slobs-cdn.slobs-cdn.streamlabs.com/obsplugin/package/slplugin-$branchName-$commitSha-signed.exe"    
		$installerResult = $false
			
		# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
		$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
		$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
		$Env:AWS_DEFAULT_REGION = "us-west-2"
	
		try {			
			Write-Host "Running aws s3 cp $installerUrl $destination"
			
			aws s3 cp $installerUrl $destination --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
			
			if ($LASTEXITCODE -ne 0) {
				throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
			}
			
			cfcli -d streamlabs.com purge --url $destinationUrl
			
			if ($LASTEXITCODE -ne 0)  {
				throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
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
    $jsonFilePath = Join-Path $folder "rev$revNumberGoing_$branchName.json"
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
		aws s3 cp $jsonFilePath s3://slobs-cdn.streamlabs.com/obsplugin/meta/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
		
		if ($LASTEXITCODE -ne 0) {
			throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
		}
		
		aws s3 cp $zipFilePath s3://slobs-cdn.streamlabs.com/obsplugin/package/	--acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
        
		if ($LASTEXITCODE -ne 0) {
			throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
		}
		
		cfcli -d streamlabs.com purge --url $("https://slobs-cdn.streamlabs.com/obsplugin/package/" + [System.IO.Path]::GetFileName($jsonFilePath))
		
		if ($LASTEXITCODE -ne 0) {
			throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
		}
		
		cfcli -d streamlabs.com purge --url $("https://slobs-cdn.streamlabs.com/obsplugin/package/" + [System.IO.Path]::GetFileName($zipFilePath))
		
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

	if ($LASTEXITCODE -ne 0) {
		throw "Problem publishing new metadata for each version, not publishing new publish.josn"
	}
	
	Write-Host "Formatting the next meta_publish.json file..."
	
	try {
		# URL to the JSON data for revisions
		$urlJsonObsVersions = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

		# Download the JSON
		$filepathJsonPublish = ".\meta_publish.json"
		Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish

		# Read and parse the JSON file
		$jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json

		# Assign the value of next_rev to a variable
		$revNumberOutgoing = $jsonContent.next_rev

		# Swap around revisions and increment next_rev along the way
		$jsonContent.last_rev = $jsonContent.next_rev
		$jsonContent.next_rev += 1
		$jsonContent.new_release_rev = $revNumberOutgoing

		# Convert the updated object back to JSON
		$updatedJson = $jsonContent | ConvertTo-Json

		# Save the updated JSON back to the same file
		Write-Output $updatedJson
		$updatedJson | Out-File -FilePath $filepathJsonPublish	
	}
	catch {
		throw "Error: An error occurred. Details: $($_.Exception.Message)"
	}
		
	Write-Host "Uploading meta_publish.json file..."
	aws s3 cp $filepathJsonPublish s3://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
			
	if ($LASTEXITCODE -ne 0) {
		throw "On trying to upload meta_publish.json, AWS CLI returned a non-zero exit code: $LASTEXITCODE"
	}
}