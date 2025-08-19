# URL to the JSON data
$jsonUrl = "https://slobs-cdn.streamlabs.com/obsplugin/obsversions_internal.json"

# Fetch the JSON content
$jsonContent = Invoke-RestMethod -Uri $jsonUrl
$branchNames = $jsonContent.obsversions.PSObject.Properties.Value

# Iterate through each branch
foreach ($branchName in $branchNames) {
    Write-Host "Processing branch: $branchName"

    # Checkout the branch
    git checkout $branchName

    # Attempt to merge with main branch
    $mergeResult = git merge main --no-ff 2>&1
    if ($mergeResult -match "CONFLICT") {
        Write-Host "Conflict in branch $branchName. Please resolve the conflicts manually."

        # Get list of files that were in conflict
        $conflictFiles = git diff --name-only --diff-filter=U

        $conflictsResolved = $false
        while (-not $conflictsResolved) {
            Read-Host "Press Enter to continue once you have resolved the conflicts"

            # Re-check each previously conflicted file for conflict markers
            $conflictsResolved = $true
            foreach ($file in $conflictFiles) {
                $content = Get-Content $file
                if ($content -match '<<<<<<< HEAD' -or $content -match '=======' -or $content -match '>>>>>>>') {
                    Write-Host "Unresolved conflicts detected in $file. Please resolve them."
                    $conflictsResolved = $false
                    break
                }
            }

            if ($conflictsResolved) {
                Write-Host "All conflicts resolved. Continuing..."
                # Here, you would typically add and commit the resolved files
                git add .
                git commit -m "Resolved merge conflicts for $branchName"
            } else {
                Write-Host "Please resolve all conflicts then press Enter to check again."
            }
        }
    } else {
        Write-Host "Merge completed successfully for branch $branchName."
    }

    # Attempt to push changes to remote repository after conflict resolution
    try {
        git push
        Write-Host "Pushed changes successfully for branch $branchName."
    } catch {
        Write-Host "Error occurred while pushing branch $branchName."
    }
}
