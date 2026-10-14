# Edits the rollout fields of meta_publish.json. Blank revision inputs keep the current value.
$ChanceGetNew = "$Env:ROLLOUT_CHANCE_GET_NEW"
$NewReleaseRev = "$Env:ROLLOUT_NEW_RELEASE_REV"
$StableReleaseRev = "$Env:ROLLOUT_STABLE_RELEASE_REV"

# Local environment variables, even if there are system ones with the same name, these are used for the cmd below
$Env:AWS_ACCESS_KEY_ID = $Env:AWS_RELEASE_ACCESS_KEY_ID
$Env:AWS_SECRET_ACCESS_KEY = $Env:AWS_RELEASE_SECRET_ACCESS_KEY
$Env:AWS_DEFAULT_REGION = "us-west-2"

function ParseInt($value, $name, $min, $max) {
	$parsed = 0
	if (-not [int]::TryParse($value.Trim(), [ref]$parsed) -or $parsed -lt $min -or $parsed -gt $max) {
		throw "$name must be an integer from $min to $max, got '$value'"
	}
	return $parsed
}

# Every supported OBS version must have metadata for the revision, or its users can't fetch it
function AssertRevisionPublished($rev, $branchNames) {
	$missing = @()
	foreach ($branchName in $branchNames) {
		try {
			Invoke-WebRequest -Uri "https://slobs-cdn.streamlabs.com/obsplugin/meta/rev${rev}_${branchName}.json" -Method Head -UseBasicParsing | Out-Null
		}
		catch {
			$missing += $branchName
		}
	}
	if ($missing.Count -gt 0) {
		throw "Revision $rev is not published for: $($missing -join ', ')"
	}
}

$urlMetaPublish = "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"
$filepathJsonPublish = Join-Path $PWD "meta_publish.json"
Invoke-WebRequest -Uri $urlMetaPublish -OutFile $filepathJsonPublish -UseBasicParsing
$jsonContent = Get-Content -Path $filepathJsonPublish -Raw | ConvertFrom-Json
$before = $jsonContent | ConvertTo-Json

$jsonObsVersions = Invoke-RestMethod -Uri "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions_internal.json"
$branchNames = $jsonObsVersions.obsversions.PSObject.Properties.Value

$jsonContent.chance_get_new = ParseInt $ChanceGetNew "chance_get_new" 0 100

if ($NewReleaseRev.Trim()) {
	$rev = ParseInt $NewReleaseRev "new_release_rev" 1 $jsonContent.last_rev
	AssertRevisionPublished $rev $branchNames
	$jsonContent.new_release_rev = $rev
}

if ($StableReleaseRev.Trim()) {
	$rev = ParseInt $StableReleaseRev "stable_release_rev" 1 $jsonContent.last_rev
	AssertRevisionPublished $rev $branchNames
	$jsonContent.stable_release_rev = $rev
}

$after = $jsonContent | ConvertTo-Json
Write-Output "Before:`n$before"
Write-Output "After:`n$after"

if ($Env:GITHUB_STEP_SUMMARY) {
	$fence = '```'
	"### meta_publish.json`n`nBefore:`n${fence}json`n$before`n$fence`n`nAfter:`n${fence}json`n$after`n$fence" | Out-File -FilePath $Env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
}

if ($after -eq $before) {
	Write-Output "No changes, not uploading."
	exit 0
}

$utf8NoBomEncoding = New-Object System.Text.UTF8Encoding $False
[System.IO.File]::WriteAllText($filepathJsonPublish, $after, $utf8NoBomEncoding)

aws s3 cp $filepathJsonPublish s3://slobs-cdn.streamlabs.com/obsplugin/ --acl public-read --metadata-directive REPLACE --cache-control "max-age=0, no-cache, no-store, must-revalidate"

if ($LASTEXITCODE -ne 0) {
	throw "AWS CLI returned a non-zero exit code: $LASTEXITCODE"
}

cfcli --token $Env:CF_API_TOKEN -d streamlabs.com purge --url $urlMetaPublish

if ($LASTEXITCODE -ne 0)  {
	throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
}
