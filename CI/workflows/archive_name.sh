#!/usr/bin/env bash
#
# The one place the prebuilt-OBS archive name is decided. The e2e job in tests.yml both
# publishes under this name and asks for it, so nothing here can disagree with itself - but
# a name that drifts between runs means every run silently falls back to a full OBS build.
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
# A glob will not do this: [0-9]*.[0-9]*.[0-9]* happily accepts 31x.1y.2garbage.
if ! [[ $obs_version =~ ^[0-9]+\.[0-9]+\.[0-9]+([-.][A-Za-z0-9]+)*$ ]]; then
    echo "obs.ver gave no usable version: '${obs_version}'" >&2
    exit 1
fi

echo "obs-${obs_version}-${BUILD_TYPE}-v${ARCHIVE_VERSION}-${RUNNER_LABEL}.tar.zst"
