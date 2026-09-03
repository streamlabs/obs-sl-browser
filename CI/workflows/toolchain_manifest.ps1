<#
.SYNOPSIS
    Records what the prebuilt OBS tree was built against, into obs-studio\.prebuild-manifest.json.

.DESCRIPTION
    Shipped inside the archive so a PR run can say out loud when it restored a tree built
    against a different toolset than the one it is running on. That is the case where the
    archive quietly stops helping: the restored tree rebuilds all of OBS anyway and pays the
    download on top, which is slower than not caching at all.
#>
[CmdletBinding()]
param(
    [string]$PluginDir = 'obs-sl-browser',
    [string]$ObsDir = 'obs-studio'
)

$ErrorActionPreference = 'Stop'

$ver = (Get-Content (Join-Path $PluginDir 'obs.ver') -Raw).Trim()
$archive = bash (Join-Path $PluginDir 'CI/workflows/archive_name.sh')

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -property catalog_productDisplayVersion

# Read back what MSBuild actually targeted, not the newest SDK installed on the runner. The
# OBS windows-x64 preset pins one (architecture: x64,version=10.0.22621), so an image that
# also carries a newer SDK would otherwise be recorded as having built against it - and this
# manifest exists purely to diagnose toolchain drift, so a wrong value is worse than none.
$proj = Join-Path $ObsDir 'build_x64\libobs\libobs.vcxproj'
$sdk = if (Test-Path $proj) {
    (Select-String -Path $proj -Pattern '<WindowsTargetPlatformVersion>([^<]+)' |
        Select-Object -First 1).Matches[0].Groups[1].Value
}
else { $null }

$manifest = Join-Path $ObsDir '.prebuild-manifest.json'

@{
    obs_version   = $ver
    archive       = $archive
    image_os      = $env:ImageOS
    image_version = $env:ImageVersion
    visual_studio = $vs
    windows_sdk   = $sdk
    cmake         = (cmake --version | Select-Object -First 1)
    built_at      = (Get-Date -Format 'o')
} | ConvertTo-Json | Set-Content $manifest

Get-Content $manifest
