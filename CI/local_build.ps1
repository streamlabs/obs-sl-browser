<#
.SYNOPSIS
    Compatibility entry point for a local OBS and plugin build.

.DESCRIPTION
    Uses the current plugin working copy and the OBS version pinned in obs.ver. The older
    one-shot clone recipe called OBS's removed CI/build-windows.ps1 and cannot build OBS 33.
    CI/dev_build.ps1 now owns the local build, including plugin registration and generator
    selection. Arguments such as -PluginOnly and -Run are passed through to it.
#>

& (Join-Path $PSScriptRoot 'dev_build.ps1') @args
