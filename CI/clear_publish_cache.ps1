cfcli --token $Env:CF_API_TOKEN -d streamlabs.com purge --url "https://slobs-cdn.streamlabs.com/obsplugin/meta_publish.json"

if ($LASTEXITCODE -ne 0)  {
	throw "cfcli returned a non-zero exit code: $LASTEXITCODE"
}