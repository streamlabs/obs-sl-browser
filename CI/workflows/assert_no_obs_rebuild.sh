#!/usr/bin/env bash
#
# OBS 33 must recompile obs-core-modules.c and relink obs.dll when the plugin is grafted into a
# prebuilt tree. Warn if anything else under libobs recompiles, which means the archive has
# stopped saving the expensive part of the build.
#
# Usage: CI/workflows/assert_no_obs_rebuild.sh <obs.dll mtime captured at restore time>
#
# obs.dll's mtime is the first signal. Object timestamps distinguish the expected core-module
# update from a wider rebuild.
#
set -uo pipefail

before=${1:-}
if [ -z "$before" ]; then
    echo "usage: $0 <obs.dll mtime from the restore step>" >&2
    exit 2
fi

summary=${GITHUB_STEP_SUMMARY:-/dev/null}
dll=obs-studio/build_x64/libobs/RelWithDebInfo/obs.dll

after=$(stat -c %Y "$dll")

if [ "$before" = "$after" ]; then
    echo "obs.dll untouched - the archive is doing its job." | tee -a "$summary"
    exit 0
fi

core_list=obs-studio/build_x64/libobs/obs-core-modules.c
if [ -f "$core_list" ] && grep -q '"sl-browser-plugin"' "$core_list" \
    && grep -Eq '^[[:space:]]+obs-core-modules\.c[[:space:]]*$' obs-studio/build.log; then
    other_objects=$(find obs-studio/build_x64/libobs -type f -name '*.obj' \
        -newermt "@$before" ! -path '*/libobs-core-modules.dir/*')
    if [ -z "$other_objects" ]; then
        echo "obs.dll relinked only to register sl-browser-plugin as an OBS core module." | tee -a "$summary"
        exit 0
    fi
fi

echo "::warning::libobs was relinked, so the archive is stale or the toolchain moved. Bump ARCHIVE_VERSION in CI/workflows/archive_name.sh; the next run with no archive under the new name will rebuild and republish it."
{
    echo "### Archive no longer avoiding an OBS rebuild"
    echo
    echo "\`obs.dll\` was relinked during this run. Sources compiled:"
    echo '```'
    grep -oE '^\s{2,}[A-Za-z0-9_./-]+\.(c|cpp)$' obs-studio/build.log | sort -u | head -40
    echo '```'
} >> "$summary"
