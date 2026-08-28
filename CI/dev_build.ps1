<#
.SYNOPSIS
    Builds OBS and the sl-browser plugin locally, in place, for repeated iteration.

.DESCRIPTION
    local_build.ps1 is the one-shot CI recipe: it clones obs-sl-browser, clones OBS next to it,
    copies the plugin in, and builds once. That is wrong for development - the copy means your
    edits are invisible to the build, and every run starts from scratch.

    This script instead:
      * uses the working copy this script lives in, never a clone of it
      * incrementally syncs this working copy into <ObsDir>\plugins\obs-sl-browser, so edits are
        picked up on every run (the plugin needs to sit under plugins/ because its sources include
        ..\obs-browser\panel\browser-panel-internal.hpp from OBS's own bundled browser plugin)
      * clones OBS once, then reuses it
      * configures once, then only rebuilds - re-running is incremental and fast
      * leaves the working copy clean: nothing here edits a tracked file

    Targets the OBS 30+ build system (CMakePresets windows-x64 -> build_x64). Older OBS versions
    use a different build command and are rejected before the script changes anything. OBS 31.1.0
    is the floor for dual output; below it the plugin still builds, with dual output compiled out.

.PARAMETER ObsDir
    Where the obsproject/obs-studio checkout lives. Cloned on first run, reused after.
    Defaults to builds\obs-studio-<version> inside the plugin repo. The builds directory is
    already gitignored, keeping temporary clones and dependencies out of the parent repos folder.

.PARAMETER ObsVersion
    OBS tag to build against. Defaults to the contents of obs.ver, which is what CI uses.

.PARAMETER Config
    RelWithDebInfo (default) or Debug.

.PARAMETER PluginOnly
    Build only the three sl-browser targets. This is the normal inner-loop switch - a full OBS
    build takes many minutes, the plugin alone takes a fraction of that.

.PARAMETER Reconfigure
    Re-run cmake configure. Needed after changing CMakeLists.txt, adding or removing a source
    file, or changing ObsVersion.

.PARAMETER Clean
    Delete the build directory first. Full rebuild.

.PARAMETER Run
    Install into OBS's rundir and launch it, so you can actually click through the change.

.PARAMETER Shallow
    Shallow-clone OBS on first run. Faster, but leaves no history to bisect against.

.PARAMETER Force
    Replace an unmanaged directory sitting where the synced plugin sources belong.

.EXAMPLE
    .\CI\dev_build.ps1
    First run: clone OBS, fetch deps, configure, build everything.

.EXAMPLE
    .\CI\dev_build.ps1 -PluginOnly -Run
    The inner loop: rebuild just the plugin and launch OBS with it.
#>

[CmdletBinding()]
param(
    [string]$ObsDir,
    [string]$ObsVersion,
    [ValidateSet('RelWithDebInfo', 'Debug')]
    [string]$Config = 'RelWithDebInfo',
    [string]$GrpcVersion = 'v1.58.0',
    [switch]$PluginOnly,
    [switch]$Reconfigure,
    [switch]$Clean,
    [switch]$Run,
    [switch]$Shallow,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Step($msg) { Write-Host "`n==> $msg" -ForegroundColor Cyan }
function Info($msg) { Write-Host "    $msg" -ForegroundColor DarkGray }

function ConvertTo-ObsVersion($value) {
    $match = [regex]::Match($value, '^(?:v)?(?<version>\d+\.\d+\.\d+)(?:[-+].*)?$')
    if (-not $match.Success) {
        throw "OBS version '$value' is invalid. Expected a tag such as '31.1.2' or '32.0.0-rc1'."
    }

    return [version]$match.Groups['version'].Value
}

# --- Locate the working copy -------------------------------------------------

$PluginDir = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

if (-not (Test-Path (Join-Path $PluginDir 'sl-browser-plugin.cpp'))) {
    throw "Expected the plugin repo at '$PluginDir' but sl-browser-plugin.cpp is not there. Run this script from its home in CI\."
}

if (-not $ObsVersion) {
    $verFile = Join-Path $PluginDir 'obs.ver'
    if (-not (Test-Path $verFile)) { throw "obs.ver not found at $verFile" }
    $ObsVersion = (Get-Content $verFile -Raw).Trim()
}

$minimumObsVersion = [version]'30.0.0'
$parsedObsVersion = ConvertTo-ObsVersion $ObsVersion
if ($parsedObsVersion -lt $minimumObsVersion) {
    throw "OBS $ObsVersion is not supported by this script. OBS 30.0.0 or newer is required because older versions use CI\build-windows.ps1 instead of the windows-x64 CMake preset."
}

if (-not $ObsDir) {
    $ObsDir = Join-Path $PluginDir "builds\obs-studio-$ObsVersion"
}

$obsFull = [System.IO.Path]::GetFullPath($ObsDir)
$pluginFull = [System.IO.Path]::GetFullPath($PluginDir)

$DepsDir = Join-Path (Split-Path $obsFull -Parent) 'sl-browser-deps'
$BuildDir = Join-Path $obsFull 'build_x64'

Step "Configuration"
Info "plugin      $pluginFull"
Info "obs         $obsFull  (tag $ObsVersion)"
Info "deps        $DepsDir"
Info "build       $BuildDir  ($Config)"

# --- Tools -------------------------------------------------------------------

foreach ($tool in @('git', 'cmake')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "'$tool' is not on PATH." }
}
if (-not (Get-Command '7z' -ErrorAction SilentlyContinue)) {
    throw "'7z' is not on PATH. install_deps.cmd needs it to unpack the gRPC dependency."
}

# --- OBS checkout ------------------------------------------------------------

if (-not (Test-Path (Join-Path $obsFull '.git'))) {
    Step "Cloning OBS $ObsVersion (once; later runs reuse it)"
    New-Item -ItemType Directory -Path (Split-Path $obsFull -Parent) -Force | Out-Null
    $cloneArgs = @('clone', '--recursive', '--branch', $ObsVersion)
    if ($Shallow) { $cloneArgs += @('--depth', '1', '--shallow-submodules') }
    $cloneArgs += @('https://github.com/obsproject/obs-studio.git', $obsFull)

    git @cloneArgs
    if ($LASTEXITCODE -ne 0) { throw "git clone failed ($LASTEXITCODE)" }
}
else {
    Step "Reusing OBS checkout"
    Push-Location $obsFull
    try {
        $described = (git describe --tags --always 2>$null)
        Info "at $described"
        if ($described -and (ConvertTo-ObsVersion $described) -lt $minimumObsVersion) {
            throw "The OBS checkout at '$obsFull' is $described. OBS 30.0.0 or newer is required by this script."
        }
        if ($described -and $described -ne $ObsVersion) {
            Write-Warning "Checkout is at '$described' but obs.ver says '$ObsVersion'. Checkout the right tag, or pass -ObsVersion, if that is not deliberate."
        }
        git submodule update --init --recursive
        if ($LASTEXITCODE -ne 0) { throw "git submodule update failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }
}

# --- gRPC dependency ---------------------------------------------------------

Step "gRPC dependency"
New-Item -ItemType Directory -Path $DepsDir -Force | Out-Null
Push-Location $DepsDir
try {
    $env:GRPC_VERSION = $GrpcVersion
    & (Join-Path $PluginDir 'ci\install_deps.cmd')
    if ($LASTEXITCODE -ne 0) { throw "install_deps.cmd failed ($LASTEXITCODE)" }
}
finally { Pop-Location }

$grpcDist = Join-Path $DepsDir 'grpc_dist'
if (-not (Test-Path $grpcDist)) { throw "grpc_dist missing at $grpcDist after install_deps." }

$env:Protobuf_DIR = Join-Path $grpcDist 'cmake'
$env:absl_DIR = Join-Path $grpcDist 'lib\cmake\absl'
$env:gRPC_DIR = Join-Path $grpcDist 'lib\cmake\grpc'
$env:utf8_range_DIR = Join-Path $grpcDist 'lib\cmake\utf8_range'

# --- Sync the working copy into plugins/ -------------------------------------

Step "Syncing the working copy into plugins/"
$pluginSourceDir = Join-Path $obsFull 'plugins\obs-sl-browser'
$syncMarker = Join-Path $pluginSourceDir '.dev_build_managed'
$syncManifest = Join-Path $pluginSourceDir '.dev_build_files'

if (Test-Path $pluginSourceDir) {
    $item = Get-Item $pluginSourceDir -Force
    if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) {
        # Remove only the old junction left by an earlier version of this script.
        [System.IO.Directory]::Delete($pluginSourceDir, $false)
        New-Item -ItemType Directory -Path $pluginSourceDir | Out-Null
    }
    elseif (-not (Test-Path $syncMarker)) {
        if (-not $Force) {
            throw @"
'$pluginSourceDir' is an unmanaged directory, most likely a leftover copy from local_build.ps1.
Check what is in it, then re-run with -Force to replace it with a managed source copy.
"@
        }

        Write-Warning "Replacing unmanaged directory at $pluginSourceDir"
        $expectedParent = [System.IO.Path]::GetFullPath((Join-Path $obsFull 'plugins')).TrimEnd('\') + '\'
        $resolvedSourceDir = [System.IO.Path]::GetFullPath($pluginSourceDir)
        if (-not $resolvedSourceDir.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected plugin directory '$resolvedSourceDir'."
        }
        Remove-Item -LiteralPath $resolvedSourceDir -Recurse -Force
        New-Item -ItemType Directory -Path $pluginSourceDir | Out-Null
    }
}
else {
    New-Item -ItemType Directory -Path $pluginSourceDir | Out-Null
}

Set-Content -LiteralPath $syncMarker -Value 'Managed by CI\dev_build.ps1.'

# Git supplies the working set so ignored build output and nested repositories are not copied.
$sourceFiles = @(git -C $pluginFull ls-files --cached --others --exclude-standard)
if ($LASTEXITCODE -ne 0) { throw "git ls-files failed ($LASTEXITCODE)" }
$sourceFiles = @($sourceFiles | Where-Object {
    $_ -and (Test-Path -LiteralPath (Join-Path $pluginFull $_) -PathType Leaf)
} | Sort-Object -Unique)

$sourceRoot = [System.IO.Path]::GetFullPath($pluginSourceDir).TrimEnd('\') + '\'
$currentFiles = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach ($relativePath in $sourceFiles) { [void]$currentFiles.Add($relativePath) }

if (Test-Path $syncManifest) {
    foreach ($relativePath in Get-Content $syncManifest) {
        if (-not $relativePath -or $currentFiles.Contains($relativePath)) { continue }
        $stalePath = [System.IO.Path]::GetFullPath((Join-Path $pluginSourceDir $relativePath))
        if (-not $stalePath.StartsWith($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected stale path '$stalePath'."
        }
        if (Test-Path -LiteralPath $stalePath -PathType Leaf) {
            Remove-Item -LiteralPath $stalePath -Force
        }
    }
}

$copied = 0
foreach ($relativePath in $sourceFiles) {
    $sourcePath = Join-Path $pluginFull $relativePath
    $destinationPath = [System.IO.Path]::GetFullPath((Join-Path $pluginSourceDir $relativePath))
    if (-not $destinationPath.StartsWith($sourceRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to copy to unexpected path '$destinationPath'."
    }

    $sourceItem = Get-Item -LiteralPath $sourcePath
    $needsCopy = -not (Test-Path -LiteralPath $destinationPath -PathType Leaf)
    if (-not $needsCopy) {
        $destinationItem = Get-Item -LiteralPath $destinationPath
        $needsCopy = $sourceItem.Length -ne $destinationItem.Length -or
            $sourceItem.LastWriteTimeUtc -ne $destinationItem.LastWriteTimeUtc
    }

    if ($needsCopy) {
        New-Item -ItemType Directory -Path (Split-Path $destinationPath -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $sourcePath -Destination $destinationPath -Force
        $copied++
    }
}

Set-Content -LiteralPath $syncManifest -Value $sourceFiles
Info "$copied changed file(s) copied; $($sourceFiles.Count) file(s) tracked"

# --- Register the plugin with OBS's build ------------------------------------

$pluginsCMake = Join-Path $obsFull 'plugins\CMakeLists.txt'
$addLine = 'add_subdirectory(obs-sl-browser)'

if (-not (Select-String -Path $pluginsCMake -SimpleMatch -Pattern $addLine -Quiet)) {
    Add-Content -Path $pluginsCMake -Value $addLine
    Info "registered in plugins/CMakeLists.txt"
    # A new subdirectory is invisible to an existing cache.
    $Reconfigure = $true
}
else {
    Info "already registered in plugins/CMakeLists.txt"
}

# --- Configure ---------------------------------------------------------------

if ($Clean -and (Test-Path $BuildDir)) {
    Step "Removing $BuildDir"
    Remove-Item $BuildDir -Recurse -Force
}

$cacheFile = Join-Path $BuildDir 'CMakeCache.txt'
if ($Reconfigure -or -not (Test-Path $cacheFile)) {
    Step "Configuring"

    # The preset pins a Windows SDK that is not installed everywhere; fall back to the newest present.
    $configureArgs = @('--preset', 'windows-x64', '-DCMAKE_COMPILE_WARNING_AS_ERROR=OFF')

    $presetSdk = '10.0.22621.0'
    $sdkRoot = 'C:\Program Files (x86)\Windows Kits\10\Include'
    if (Test-Path $sdkRoot) {
        $installed = Get-ChildItem $sdkRoot -Directory | Select-Object -ExpandProperty Name
        if ($installed -notcontains $presetSdk) {
            $newest = $installed | Where-Object { $_ -match '^10\.' } | Sort-Object { [version]$_ } | Select-Object -Last 1
            if (-not $newest) { throw "No Windows 10/11 SDK found under $sdkRoot" }
            Write-Warning "Preset wants SDK $presetSdk, which is not installed. Using $newest."
            $configureArgs += @('-A', "x64,version=$newest")
        }
    }

    Push-Location $obsFull
    try {
        cmake @configureArgs
        if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
    }
    finally { Pop-Location }
}
else {
    Step "Skipping configure (cache present; pass -Reconfigure to force)"
}

# --- Build -------------------------------------------------------------------

Step "Building"
$buildArgs = @('--build', $BuildDir, '--config', $Config)
if ($PluginOnly) {
    $buildArgs += @('--target', 'sl-browser-plugin', 'sl-browser', 'sl-browser-page')
    Info "plugin targets only"
}

$sw = [Diagnostics.Stopwatch]::StartNew()
cmake @buildArgs
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
$sw.Stop()

# --- Verify ------------------------------------------------------------------

Step "Artifacts"
$outDir = Join-Path $BuildDir "plugins\obs-sl-browser\$Config"
$required = @('sl-browser.exe', 'sl-browser-page.exe', 'sl-browser-plugin.dll')

$missing = @()
foreach ($f in $required) {
    $p = Join-Path $outDir $f
    if (Test-Path $p) { Info "$f  $([math]::Round((Get-Item $p).Length / 1MB, 2)) MB" }
    else { $missing += $p }
}
if ($missing.Count) { throw "Build reported success but these are missing:`n  $($missing -join "`n  ")" }

Info ""
Info "plugin output  $outDir"
Write-Host "`nBuilt in $([math]::Round($sw.Elapsed.TotalSeconds, 1))s" -ForegroundColor Green

# --- Run ---------------------------------------------------------------------

if ($Run) {
    Step "Installing into rundir"
    # Explicit prefix: rundir is populated from an absolute OBS_OUTPUT_DIR regardless, but without
    # this the default prefix is C:\Program Files and the install would need elevation.
    cmake --install $BuildDir --config $Config --prefix (Join-Path $BuildDir 'install')
    if ($LASTEXITCODE -ne 0) { throw "cmake --install failed ($LASTEXITCODE)" }
}

$obsExe = Get-ChildItem (Join-Path $BuildDir 'rundir') -Filter 'obs64.exe' -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($obsExe) {
    Write-Host "`nOBS executable:`n  $($obsExe.FullName)" -ForegroundColor Green
}
elseif ($Run) {
    throw "obs64.exe not found under $BuildDir\rundir after install."
}
else {
    Write-Warning "obs64.exe was not found under $BuildDir\rundir. Run a full build without -PluginOnly first."
}

if ($Run) {
    Step "Launching $($obsExe.FullName)"
    # OBS resolves its data relative to the working directory.
    Start-Process -FilePath $obsExe.FullName -WorkingDirectory $obsExe.DirectoryName
}
