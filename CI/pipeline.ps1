param(
    [string]$github_workspace,
    [string]$revision
)

Write-Output "Workspace is $github_workspace"
Write-Output "Revision is $revision"

$env:Protobuf_DIR = "${github_workspace}\..\grpc_dist\cmake"
$env:absl_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\absl"
$env:gRPC_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\grpc"
$env:utf8_range_DIR = "${github_workspace}\..\grpc_dist\lib\cmake\utf8_range"

# We start inside obs-sl-browser folder, move up to make room for cloning OBS and moving obs-sl-browser into it
cd ..\

# Deps
.\obs-sl-browser\ci\install_deps.cmd

# Read the obs.ver file to get the branch name
$branchName = Get-Content -Path ".\obs-sl-browser\obs.ver" -Raw

# Clone obs-studio repository with the branch name
git clone --recursive --branch $branchName https://github.com/obsproject/obs-studio.git

# Rename 'obs-studio' folder to name of git tree so pdb's know which version it was built with
Rename-Item -Path ".\obs-studio" -NewName $revision

# Update submodules in obs-studio
cd $revision
git submodule update --init --recursive

# Add new line to CMakeLists.txt in obs-studio\plugins
$cmakeListsPath = ".\plugins\CMakeLists.txt"
$addSubdirectoryLine = "add_subdirectory(obs-sl-browser)"
Add-Content -Path $cmakeListsPath -Value $addSubdirectoryLine

# Move obs-sl-browser folder into obs-studio\plugins
Copy-Item -Path "..\obs-sl-browser" -Destination ".\plugins\obs-sl-browser" -Recurse

# Build
.\CI\build-windows.ps1

# Copy platforms folder to plugin release fodler
Copy-Item -Path ".\build64\rundir\RelWithDebInfo\bin\64bit\platforms" -Destination ".\build64\plugins\obs-sl-browser\RelWithDebInfo" -Recurse

# Clone symbols store scripts
Write-Output "-- Symbols"
cd ..\
git clone --recursive --branch "no-http-source" https://github.com/stream-labs/symsrv-scripts.git

# Run symbols
cd symsrv-scripts
.\main.ps1 -localSourceDir "${github_workspace}\..\${revision}"