#!/usr/bin/env bash
#
# The restored archive only pays for itself while nothing on the OBS side recompiles. If that
# regresses the job still passes, just slowly, so it would rot unnoticed - this says it out
# loud instead. Warns rather than fails: a stale archive is a performance problem, not a
# correctness one.
#
# Usage: CI/workflows/assert_no_obs_rebuild.sh <obs.dll mtime captured at restore time>
#
# obs.dll's mtime is the exact signal. The compiled-source list is only for diagnosis.
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

echo "::warning::libobs was relinked, so the archive is stale or the toolchain moved. Bump ARCHIVE_VERSION in CI/workflows/archive_name.sh; the next run with no archive under the new name will rebuild and republish it."
{
    echo "### Archive no longer avoiding an OBS rebuild"
    echo
    echo "\`obs.dll\` was relinked during this run. Sources compiled:"
    echo '```'
    grep -oE '^\s{2,}[A-Za-z0-9_./-]+\.(c|cpp)$' obs-studio/build.log | sort -u | head -40
    echo '```'
} >> "$summary"
