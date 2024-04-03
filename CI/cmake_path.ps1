# Get the current PATH environment variable
$currentPath = [Environment]::GetEnvironmentVariable("PATH", [EnvironmentVariableTarget]::Process)
Write-Output $currentPath
Write-Output "Becomes..."

# Split the PATH into an array of individual paths
$pathArray = $currentPath -split ";"

# Filter out any paths that contain 'CMake'
$filteredPathArray = $pathArray -notmatch '\\CMake\\'

# Add custom CMake path at the beginning
$customCMakePath = "C:\github\obs30\plugins\obs-sl-browser\tools\cmake\bin"
$newPath = $customCMakePath + ";" + ($filteredPathArray -join ";")

# Set the modified PATH environment variable
[Environment]::SetEnvironmentVariable("PATH", $newPath, [EnvironmentVariableTarget]::Process)
Write-Output $newPath