# Clone the GitHub repository locally (adjust the path as needed)
$repoPath = ".\obs-sl-browser-temp"
$repoUrl = "https://github.com/streamlabs/obs-sl-browser.git"

# Ensure the directory does not already exist to avoid errors
if (-Not (Test-Path -Path $repoPath)) {
    git clone $repoUrl $repoPath
}

# URL to the JSON data for known OBS versions
$urlJsonObsVersions = "https://slobs-cdn.streamlabs.com/obsplugin/obsversions_internal.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $urlJsonObsVersions
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Store the results
$results = @()

# Iterate through each branch in the OBS versions
foreach ($branch in $branchNames) {
    # Ensure the branch exists locally, checkout to make sure it's up to date
    git -C $repoPath fetch --all
    git -C $repoPath checkout $branch
    git -C $repoPath pull

    # Fetch the last 25 revisions for the branch
    $commits = git -C $repoPath log -25 --pretty=format:"%H %ct" | ForEach-Object {
        $parts = $_ -split " "
        @{
            sha = $parts[0]
            unixTimeStamp = $parts[1]
        }
    }

    Write-Output "Scanning..."
	
    foreach ($commit in $commits) {
		$commitSha = $commit.sha
			
		try {
			$url = "https://slobs-cdn.streamlabs.com/obsplugin/meta_sha/$commitSha.json"
			$outputFile = ".\temp.json"

			Invoke-WebRequest -Uri $url -OutFile $outputFile
			
			Get-Content "$PWD\temp.json" -Encoding Unicode | Set-Content -Encoding UTF8 "$PWD\temp2.json"

			$MyRawString = Get-Content -Raw "$PWD\temp2.json"
			$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $False
			[System.IO.File]::WriteAllLines("$PWD\temp3.json", $MyRawString, $Utf8NoBomEncoding)

			# Read temp3.json into a JSON object
			$temp3Json = Get-Content "$PWD\temp3.json" | ConvertFrom-Json
			
			Remove-Item "$PWD\temp.json"
			Remove-Item "$PWD\temp2.json"
			Remove-Item "$PWD\temp3.json"	
		
			if ($temp3Json.rev) {
				# If the build exists, prepare the data
				$data = @{
					branchName = $branch
					commitSha = $commitSha
					unixTimeStamp = $commit.unixTimeStamp
					revision = $temp3Json.rev
				}
				Write-Output "ADDING: $commitSha"
				$results += $data
			}
		
		}
		catch { 
		
		}
	}
}

# Convert the results to JSON
$jsonResults = $results | ConvertTo-Json
Write-Output $jsonResults

$Utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $False
[System.IO.File]::WriteAllLines("$PWD\internal_meta.json", $jsonResults, $Utf8NoBomEncoding)

# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
$Env:AWS_DEFAULT_REGION = "us-west-2"
	
Write-Host "Uploading to AWS"

aws s3 cp "$PWD\internal_meta.json" s3://slobs-cdn.streamlabs.com/obsplugin/meta/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"

if ($LASTEXITCODE -ne 0) {
	throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
}

cfcli -d streamlabs.com purge --url "https://slobs-cdn.streamlabs.com/obsplugin/meta/internal_meta.json"

if ($LASTEXITCODE -ne 0)  {
	throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
}
