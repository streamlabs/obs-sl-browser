#!/usr/bin/env bash
#
# Everything to do with the prebuilt OBS archive, in one place:
#
#   exists    is it already published?      -> prints yes|no, sets skip= in $GITHUB_OUTPUT
#   create    tar+zstd ./obs-studio         -> writes the archive into the current directory
#   publish   upload it to the release      -> creates the release on first use
#   restore   download and unpack it        -> sets hit= and obsdll= in $GITHUB_OUTPUT
#
# Run from the directory that holds (or will hold) obs-studio. On CI that is the workspace
# root, and it has to be the same path in both workflows: CMakeCache.txt pins absolute paths,
# so a tree restored somewhere else is unusable and cmake fails outright.
#
# bash rather than PowerShell throughout because PowerShell corrupts binary through a pipe.
#
# Every gh call passes -R. The workspace root is not a git repository - the plugin is checked
# out into a subdirectory - so without it gh tries to infer the repo from git and dies with
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

exists)
    if [ "${FORCE:-}" = "true" ]; then
        echo "skip=false" >> "$out"
        echo "no (forced)"
        exit 0
    fi
    if gh release view "$tag" -R "$repo" --json assets --jq '.assets[].name' 2>/dev/null |
         grep -qx "$name"; then
        echo "skip=true" >> "$out"
        echo "::notice::$name is already published. Re-run with force to rebuild."
        echo yes
    else
        echo "skip=false" >> "$out"
        echo no
    fi
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
        gh release create "$tag" -R "$repo" --prerelease \
            --target "${GITHUB_SHA:-main}" \
            --title "Prebuilt OBS trees" \
            --notes "Prebuilt OBS build trees for CI. Published by .github/workflows/obs-prebuild.yml, consumed by the e2e job in tests.yml. Not a product release." || exit 1
    fi
    gh release upload "$tag" "$name" -R "$repo" --clobber || exit 1
    echo "Published \`$name\`" >> "$summary"
    echo "Published $name"
    ;;

restore)
    echo "Looking for $name on release $tag"
    if ! gh release download "$tag" -R "$repo" -p "$name" -D . 2>/dev/null; then
        echo "hit=false" >> "$out"
        echo "::notice::$name is not published - building OBS from scratch. Run the 'OBS prebuild' workflow to create it."
        exit 0
    fi
    zstd -dc "$name" | tar -xf - || exit 1
    rm -f "$name"
    echo "hit=true" >> "$out"
    echo "Restored \`$name\`" >> "$summary"
    # Stamped now so assert_no_obs_rebuild.sh can prove libobs was never relinked.
    echo "obsdll=$(stat -c %Y obs-studio/build_x64/libobs/RelWithDebInfo/obs.dll)" >> "$out"
    cat obs-studio/.prebuild-manifest.json 2>/dev/null || true
    ;;

*)
    echo "usage: $0 {exists|create|publish|restore}" >&2
    exit 2
    ;;
esac
