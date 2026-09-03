#!/usr/bin/env bash
#
# The one place the prebuilt-OBS archive name is decided. obs-prebuild.yml publishes under
# this name and tests.yml asks for it; if the two ever disagree every PR silently falls back
# to a full OBS build, so they share this script rather than each formatting their own.
#
# Bump ARCHIVE_VERSION whenever anything about how OBS is built changes - compiler flags,
# preset, the configure sequence, the set of directories the archive carries. Old archives
# keep their old name and are simply never requested again, so nothing has to be deleted.
#
set -euo pipefail

ARCHIVE_VERSION=1
BUILD_TYPE=RelWithDebInfo

# The image label, not just the OS: a build made on a different image can carry a different
# MSVC toolset, and a mismatched toolset means the restored tree rebuilds all of OBS.
RUNNER_LABEL=windows-2022

# ../.. because this lives in CI/workflows/; obs.ver is at the repo root.
obs_version=$(tr -d '[:space:]' < "$(dirname "$0")/../../obs.ver")

# An empty or malformed version would otherwise become a plausible-looking name that simply
# never matches a published asset, and every run would quietly fall back to a full build.
case "$obs_version" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *)
        echo "obs.ver gave no usable version: '${obs_version}'" >&2
        exit 1
        ;;
esac

echo "obs-${obs_version}-${BUILD_TYPE}-v${ARCHIVE_VERSION}-${RUNNER_LABEL}.tar.zst"
