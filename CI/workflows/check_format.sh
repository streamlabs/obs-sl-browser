#!/usr/bin/env bash
#
# Reports every first-party file clang-format would change and leaves a clang-format.patch
# behind that fixes them. Run from the repo root. Exits 1 when anything needs reformatting;
# the workflow marks that step continue-on-error so it reports without reding the PR.
#
# Runnable locally:  CLANG_FORMAT_VERSION=19.1.5 CI/check_format.sh
#
set -uo pipefail

version=${CLANG_FORMAT_VERSION:-19.1.5}

# Written to only when running under Actions, so a local run does not need them set.
summary=${GITHUB_STEP_SUMMARY:-/dev/null}

# deps/ is vendored third party (minizip, json11) and has never been formatted to this
# config; gating it would mean this can never pass. builds/ is not tracked.
mapfile -t files < <(git ls-files -- '*.cpp' '*.h' '*.hpp' | grep -v '^deps/')

if [ ${#files[@]} -eq 0 ]; then
    echo "no first-party sources found - is this the repo root?" >&2
    exit 1
fi

# Line-level annotations so violations land on the Files tab. Capped because GitHub renders
# only the first handful per run anyway. --Werror is deliberately absent: it makes
# clang-format print "error:" instead of "warning:", which this filter would not match, and
# the verdict comes from the diff below regardless.
clang-format --dry-run "${files[@]}" 2> warnings.txt || true
grep -E ':[0-9]+:[0-9]+: warning:' warnings.txt | head -n 20 |
    sed -nE 's|^(.+):([0-9]+):([0-9]+): warning: (.*)$|::warning file=\1,line=\2,col=\3::\4|p'

clang-format -i "${files[@]}"

if git diff --quiet; then
    echo "All ${#files[@]} first-party files match .clang-format ($version)." >> "$summary"
    echo "All ${#files[@]} first-party files match .clang-format ($version)."
    exit 0
fi

git diff > clang-format.patch

{
    echo "### clang-format $version"
    echo
    echo "$(git diff --name-only | wc -l) of ${#files[@]} first-party files need reformatting."
    echo
    echo "Download the **clang-format-patch** artifact and \`git apply clang-format.patch\`, or run:"
    echo
    echo '```'
    echo "pip install clang-format==$version"
    echo "git ls-files -- '*.cpp' '*.h' '*.hpp' | grep -v '^deps/' | xargs clang-format -i"
    echo '```'
    echo
    echo "| File | Lines |"
    echo "| --- | ---: |"
    git diff --numstat | awk '{print $1+$2, $3}' | sort -rn | awk '{printf "| %s | %s |\n", $2, $1}'
} >> "$summary"

git diff --stat
exit 1
