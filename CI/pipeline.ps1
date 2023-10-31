param(
    [string]$github_workspace
)

Write-Output "Workspace is $github_workspace"

$env:Protobuf_DIR = "${github_workspace}\..\grpc_dist\cmake"
$env:absl_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\absl"
$env:gRPC_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\grpc"

# Access environment variables in PowerShell
$Protobuf_DIR = $env:Protobuf_DIR
$absl_DIR = $env:absl_DIR
$gRPC_DIR = $env:gRPC_DIR

# Example usage
Write-Output "Protobuf_DIR is $Protobuf_DIR"
Write-Output "absl_DIR is $absl_DIR"
Write-Output "gRPC_DIR is $gRPC_DIR"

# We start inside obs-sl-browser folder, move up to make room for cloning OBS and moving obs-sl-browser into it
cd ..\

# Deps
.\obs-sl-browser\ci\install_deps.cmd

# Read the obs.ver file to get the branch name
$branchName = Get-Content -Path ".\obs-sl-browser\obs.ver" -Raw

# Clone obs-studio repository with the branch name
git clone --recursive --branch $branchName https://github.com/obsproject/obs-studio.git

# Update submodules in obs-studio
cd obs-studio
git submodule update --init --recursive

# Add new line to CMakeLists.txt in obs-studio\plugins
$cmakeListsPath = ".\plugins\CMakeLists.txt"
$addSubdirectoryLine = "add_subdirectory(obs-sl-browser)"
Add-Content -Path $cmakeListsPath -Value $addSubdirectoryLine

# Move obs-sl-browser folder into obs-studio\plugins
Copy-Item -Path "..\obs-sl-browser" -Destination ".\plugins\obs-sl-browser" -Recurse

# Build
.\CI\build-windows.ps1