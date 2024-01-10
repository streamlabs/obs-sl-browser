# URL to the JSON data
$jsonUrl = "https://s3.us-west-2.amazonaws.com/slobs-cdn.streamlabs.com/obsplugin/obsversions.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Iterate through each branch
foreach ($branchName in $branchNames) {
    Write-Host "Processing branch: $branchName"

    # Forcefully checkout the branch
    git checkout -f $branchName

    # Fetch latest changes from main branch
    git fetch origin main

    # Attempt to merge with main branch
    $mergeResult = git merge origin/main --no-ff -X theirs

    # Check if merge had conflicts
    if ($mergeResult -match "CONFLICT") {
        Write-Host "Conflict in branch $branchName. Resolving by using 'theirs' strategy."
        
        # Checkout the conflicted files using 'theirs' strategy
        git checkout --theirs .
        git add .

        # Commit the resolved merge
        git commit -m "Resolved merge conflict by using the version from main branch"
    }
    
    Write-Host "Merge completed successfully for branch $branchName."

    # Push changes to remote repository
    try {
        git push
        Write-Host "Pushed changes successfully for branch $branchName."
    } catch {
        Write-Host "Error occurred while pushing branch $branchName $_"
    }
}
