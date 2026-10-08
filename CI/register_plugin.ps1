<#
.SYNOPSIS
    Registers sl-browser-plugin in an OBS checkout's plugins/CMakeLists.txt.

.DESCRIPTION
    OBS 33 generates its core module list at set_obs_core_modules(). The plugin must be added
    and enabled before that call so a locally built OBS loads it from core/sl-browser-plugin.
    Returns $true when the file changed, so callers can reconfigure an existing build tree.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ObsDir
)

$ErrorActionPreference = 'Stop'

$pluginsCMake = Join-Path $ObsDir 'plugins\CMakeLists.txt'
if (-not (Test-Path -LiteralPath $pluginsCMake -PathType Leaf)) {
    throw "OBS plugins/CMakeLists.txt not found at '$pluginsCMake'."
}

$addLine = 'add_subdirectory(obs-sl-browser)'
$enableLine = 'target_enable(sl-browser-plugin)'
$activeAddPattern = '^[ \t]*add_subdirectory[ \t]*\([ \t]*obs-sl-browser[ \t]*\)[ \t]*(?:#.*)?$'
$activeEnablePattern = '^[ \t]*target_enable[ \t]*\([ \t]*sl-browser-plugin[ \t]*\)[ \t]*(?:#.*)?$'
$coreModulesPattern = '^[ \t]*set_obs_core_modules[ \t]*\([ \t]*\)[ \t]*(?:#.*)?$'
$originalCMake = [System.IO.File]::ReadAllText($pluginsCMake)
$lineEnding = if ($originalCMake.Contains("`r`n")) { "`r`n" } else { "`n" }
$cmakeLines = [System.Collections.Generic.List[string]]::new()

# An earlier script may have appended the plugin after the generated list or placed it at the
# top of the file. Move either registration to the correct point without duplicating it.
foreach ($line in ($originalCMake -split '\r?\n')) {
    if ($line -match $activeAddPattern -or $line -match $activeEnablePattern) { continue }
    $cmakeLines.Add($line)
}

$coreModulesIndex = -1
for ($i = 0; $i -lt $cmakeLines.Count; $i++) {
    if ($cmakeLines[$i] -match $coreModulesPattern) {
        if ($coreModulesIndex -ge 0) { throw "Multiple set_obs_core_modules() calls in '$pluginsCMake'." }
        $coreModulesIndex = $i
    }
}

if ($coreModulesIndex -ge 0) {
    $cmakeLines.Insert($coreModulesIndex, $addLine)
    $cmakeLines.Insert($coreModulesIndex + 1, $enableLine)
}
else {
    # OBS releases before the core module list only need the subdirectory.
    $cmakeLines.Add($addLine)
}

$updatedCMake = $cmakeLines -join $lineEnding
if ($updatedCMake -cne $originalCMake) {
    [System.IO.File]::WriteAllText($pluginsCMake, $updatedCMake, [System.Text.UTF8Encoding]::new($false))
    Write-Host 'registered in plugins/CMakeLists.txt'
    return $true
}

Write-Host 'already registered in plugins/CMakeLists.txt'
return $false
