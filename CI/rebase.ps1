# URL to the JSON data
$jsonUrl = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Iterate through each branch
foreach ($branchName in $branchNames) {
    Write-Host "Processing branch: $branchName"

    # Checkout the branch
    git checkout $branchName

    # Attempt to merge with main branch
    $mergeResult = git merge main --no-ff

    # Check if merge had conflicts
    if ($mergeResult -match "CONFLICT") {
        Write-Host "Conflict in branch $branchName. Skipping merge for this branch."
        # Abort the merge to clean up the state
        git merge --abort
    }
    else {
        Write-Host "Merge completed successfully for branch $branchName."

        # Push changes to remote repository
        try {
            git push
            Write-Host "Pushed changes successfully for branch $branchName."
        } catch {
            Write-Host "Error occurred while pushing branch $branchName $_"
        }
    }
}
