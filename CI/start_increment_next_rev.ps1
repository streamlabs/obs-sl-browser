param(
    [string]$token
)

# Repository details
$owner = "stream-labs"
$repo = "obs-sl-browser"
$workflowFileName = "increment_next_rev.yml"
$branch = "main"

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
	throw "Error triggering workflow: $_"
}
