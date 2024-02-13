param(
    [string]$token
)

#
# Increment next rev in meta_publish
#

$urlMetaPublish = "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

# Read and parse the initial JSON file
$filepathJsonPublish = "$env:TEMP\meta_publish.json"
Invoke-WebRequest -Uri $urlMetaPublish -OutFile $filepathJsonPublish
$initialJsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
$initialRev = $initialJsonContent.next_rev

./ci/start_increment_next_rev.ps1 $token

if ($LASTEXITCODE -ne 0) {
    throw "start_increment_next_rev.ps1 failed: $LASTEXITCODE"
}

# Wait for the next_rev value to be incremented
$checkPassed = $false

do {
	Write-Output "Checking for next_rev increment..."

	try {
		$tempjson = "$env:TEMP\temp.json"
		Invoke-WebRequest -Uri $urlMetaPublish -OutFile $tempjson
		$tempjsonContent = Get-Content -Path $tempjson -Raw | ConvertFrom-Json
		Write-Output $tempjsonContent
		$currentRev = $tempjsonContent.next_rev
		$checkPassed = $currentRev -gt $initialRev
	}
	catch {
		Write-Output "Erroring checking for next_rev, $_"
	}

	Start-Sleep -Seconds 5
}
while (-not $checkPassed)

Write-Output "The next_rev value has been incremented successfully."

#
# Start builds
#

# URL to the OBS Versions JSON data
$jsonUrl = "https://slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Iterate through each branch
foreach ($branchName in $branchNames) {
	Write-Host "Processing branch: $branchName"
	
	# Repository details
	$owner = "stream-labs"
	$repo = "obs-sl-browser"
	$workflowFileName = "main.yml"
	$branch = "$branchName"

	# GitHub API URL for triggering the workflow
	$apiUrl = "https://api.github.com/repos/$owner/$repo/actions/workflows/$workflowFileName/dispatches"

	# Header with Authorization and Accept
	$headers = @{
		Authorization = "token $token"
		Accept = "application/vnd.github.v3+json"
	}

	# Payload with ref (branch)
	$body = @{
		ref = $branch
	} | ConvertTo-Json

	# Invoke the API request
	try {
		Invoke-RestMethod -Uri $apiUrl -Method Post -Headers $headers -Body $body -ContentType "application/json"
		Write-Host "Workflow triggered successfully on branch $branch"
	} catch {
		Write-Host "Error triggering workflow: $_"
	}
}
