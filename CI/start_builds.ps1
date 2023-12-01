param(
    [string]$token
)

# URL to the JSON data
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
