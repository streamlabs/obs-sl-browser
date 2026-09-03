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

obs_version=$(tr -d '[:space:]' < "$(dirname "$0")/../obs.ver")

echo "obs-${obs_version}-${BUILD_TYPE}-v${ARCHIVE_VERSION}-${RUNNER_LABEL}.tar.zst"
