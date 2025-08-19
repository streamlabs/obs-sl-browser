# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
$Env:AWS_DEFAULT_REGION = "us-west-2"

Write-Host "Formatting the next meta_publish.json to increment next_rev..."

try {
	# URL to the JSON data for revisions
	$urlJsonObsVersions = "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

	# Download the JSON
	$filepathJsonPublish = ".\meta_publish.json"
	Invoke-WebRequest -Uri $urlJsonObsVersions -OutFile $filepathJsonPublish

	# Read and parse the JSON file
	$jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json

	# Increment increment next_rev
	$jsonContent.next_rev += 1

	# Convert the updated object back to JSON
	$updatedJson = $jsonContent | ConvertTo-Json

	# Save the updated JSON back to the same file
	Write-Output $updatedJson
	$updatedJson | Out-File -FilePath $filepathJsonPublish
	
	Write-Host "Uploading $filepathJsonPublish file..."
	aws s3 cp $filepathJsonPublish s3://slobs-cdn.streamlabs.com/obsplugin/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"
		
	if ($LASTEXITCODE -ne 0) {
		throw "On trying to upload meta_publish.json, AWS CLI returned a non-zero exit code: $LASTEXITCODE"
	}
	
	cfcli -d streamlabs.com purge --url "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

	if ($LASTEXITCODE -ne 0)  {
		throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
	}
}
catch {
	throw "Error: An error occurred. Details: $($_.Exception.Message)"
}	
