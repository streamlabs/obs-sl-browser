# Clone obs-sl-browser repositor
$branchBrowserName = "update_signing_process2"
git clone --recursive --branch $branchBrowserName https://github.com/streamlabs/obs-sl-browser.git

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
Move-Item -Path "..\obs-sl-browser" -Destination ".\plugins\obs-sl-browser"

# Build
#.\CI\build-windows.ps1
$env:CI = 'true'
.\.github\scripts\Build-Windows.ps1