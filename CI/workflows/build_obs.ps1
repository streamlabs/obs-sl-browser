<#
.SYNOPSIS
    Configures and builds OBS on its own, with no plugin in the tree. For the prebuild only.

.DESCRIPTION
    Configures twice on purpose. OBS_PLUGIN_PATH is built from CMAKE_INSTALL_LIBDIR
    (cmake/windows/defaults.cmake), a GNUInstallDirs cache variable that is still empty the
    first time defaults.cmake runs against a fresh cache. So configure #1 writes
    "../..//obs-plugins/64bit" into build_x64/config/obsconfig.h and configure #2 writes
    "../../lib/obs-plugins/64bit". obsconfig.h is on the include path of all of libobs and
    obs-frontend-api, so if a PR run were the second configure it would recompile all of OBS
    and the archive would buy nothing. Settling it here ships the stable value.

    OBS_VERSION_OVERRIDE pins the other trap: versionconfig.cmake runs
    `git describe --dirty=-modified` unless it is set, and grafting the plugin dirties the
    tree, flipping OBS_VERSION and rebuilding obsversion.c on every run.
#>
[CmdletBinding()]
param(
    [string]$PluginDir = 'obs-sl-browser',
    [string]$ObsDir = 'obs-studio'
)

$ErrorActionPreference = 'Stop'

$ver = (Get-Content (Join-Path $PluginDir 'obs.ver') -Raw).Trim()
if ($ver -notmatch '^\d+\.\d+\.\d+') { throw "obs.ver gave no usable version: '$ver'" }

Push-Location $ObsDir
try {
    # The override is quoted because PowerShell parses an unquoted token beginning with a
    # dash as a parameter and does not expand variables inside it - unquoted, cmake receives
    # the literal '$ver', which is DEFINED but not <MAJOR>.<MINOR>.<PATCH>, and
    # versionconfig.cmake aborts the configure.
    $cfg = @(
        '--preset', 'windows-x64',
        '-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF',
        "-DOBS_VERSION_OVERRIDE=$ver"
    )

    cmake @cfg
    if ($LASTEXITCODE -ne 0) { throw "configure 1 failed" }

    cmake @cfg
    if ($LASTEXITCODE -ne 0) { throw "configure 2 failed" }

    cmake --build --preset windows-x64
    if ($LASTEXITCODE -ne 0) { throw "build failed" }

    if (Test-Path 'build_x64\plugins\obs-sl-browser') {
        throw "the plugin leaked into the OBS-only build tree"
    }
}
finally { Pop-Location }
