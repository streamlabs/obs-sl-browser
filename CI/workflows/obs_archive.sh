#!/usr/bin/env bash
#
# The prebuilt OBS archive, in one place:
#
#   restore   download and unpack it   -> sets hit=true|false in $GITHUB_OUTPUT
#   create    tar+zstd ./obs-studio    -> writes the archive into the current directory
#   publish   upload it to the release -> creates the release on first use
#
# The e2e job restores; when there is nothing to restore it builds OBS on its own and then
# creates and publishes, so the next run does not have to. Set FORCE=true to ignore whatever
# is published and rebuild.
#
# Run from the directory that holds (or will hold) obs-studio. On CI that is the workspace
# root, and it has to be the same path every time: CMakeCache.txt pins absolute paths, so a
# tree restored somewhere else is unusable and cmake fails outright.
#
# bash rather than PowerShell throughout because PowerShell corrupts binary through a pipe.
#
# Every gh call passes -R. The workspace root is not a git repository - the plugin is checked
# out into a subdirectory - so without it gh infers the repo from git and dies with
# "fatal: not a git repository".
#
set -uo pipefail

here=$(dirname "$0")
tag=${RELEASE_TAG:-obs-prebuilt}
repo=${GITHUB_REPOSITORY:-streamlabs/obs-sl-browser}
out=${GITHUB_OUTPUT:-/dev/null}
summary=${GITHUB_STEP_SUMMARY:-/dev/null}

name=$(bash "$here/archive_name.sh") || exit 1

case "${1:-}" in

restore)
    if [ "${FORCE:-}" = "true" ]; then
        echo "hit=false" >> "$out"
        echo "::notice::Rebuilding $name from scratch because a refresh was requested."
        exit 0
    fi

    echo "Looking for $name on release $tag"
    if ! gh release download "$tag" -R "$repo" -p "$name" -D . 2>/dev/null; then
        echo "hit=false" >> "$out"
        echo "::notice::$name is not published yet. This run will build OBS and publish it, so the next one does not have to."
        exit 0
    fi

    zstd -dc "$name" | tar -xf - || exit 1
    rm -f "$name"

    echo "hit=true" >> "$out"
    echo "Restored \`$name\`" >> "$summary"
    cat obs-studio/.prebuild-manifest.json 2>/dev/null || true
    ;;

create)
    du -sh obs-studio
    # .git goes because nothing needs it once OBS_VERSION_OVERRIDE is pinned. The downloaded
    # dependency zips go because .deps is already extracted - but that is only safe while
    # CMakeCache.txt is restored intact, since buildspec_common.cmake decides whether to
    # re-download by looking for the zip rather than for the extracted directory.
    tar -cf - \
        --exclude='obs-studio/.git' \
        --exclude='*/.deps/*.zip' \
        --exclude='obs-studio/build_x86' \
        obs-studio | zstd -T0 -3 -o "$name" || exit 1
    ls -lh "$name"
    ;;

publish)
    if ! gh release view "$tag" -R "$repo" >/dev/null 2>&1; then
        # Anchored to the default branch, not GITHUB_SHA: during a pull_request event that is
        # the ephemeral merge commit, which belongs to no branch and makes for a tag nobody
        # can find later.
        default_branch=$(gh repo view "$repo" --json defaultBranchRef --jq .defaultBranchRef.name 2>/dev/null)
        gh release create "$tag" -R "$repo" --prerelease \
            --target "${default_branch:-main}" \
            --title "Prebuilt OBS trees" \
            --notes "Prebuilt OBS build trees for CI, published and consumed by the e2e job in tests.yml. Not a product release." || exit 1
    fi

    # Re-checked here, not just at restore time: the OBS build sits between the two, so two
    # runs that both missed can both arrive. Without this the later one clobbers an archive
    # the earlier one already published, which is the overwrite this is supposed to prevent.
    # They would be equivalent trees, but "a run never overwrites a published archive" is the
    # property that makes publishing from a pull request safe, so keep it true.
    if [ "${FORCE:-}" != "true" ] &&
       gh release view "$tag" -R "$repo" --json assets --jq '.assets[].name' 2>/dev/null |
         grep -qx "$name"; then
        echo "::notice::$name was published by another run while this one was building. Keeping theirs."
        exit 0
    fi

    gh release upload "$tag" "$name" -R "$repo" --clobber || exit 1
    echo "Published \`$name\`" >> "$summary"
    echo "Published $name"
    ;;

*)
    echo "usage: $0 {restore|create|publish}" >&2
    exit 2
    ;;
esac
