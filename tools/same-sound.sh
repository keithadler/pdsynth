#!/bin/sh
#
# pdsynth - does this change alter a single sample?
#
# Copyright (C) 2026 Keith Adler
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The test suite measures spectra and character, so it cannot see a change
# smaller than its thresholds. A one part in ten million drift through the
# oscillator passes all eleven suites, which was measured rather than assumed.
# This is the check that does see it: render the demo at some earlier commit,
# render it here, compare the bytes.
#
# It compares this machine against itself, so it is safe where a committed
# hash would not be. Different compilers and architectures round differently
# and that is not a regression.
#
#   tools/same-sound.sh              against HEAD
#   tools/same-sound.sh v0.3.0       against a tag
#
# Use it for refactors that are meant to change nothing. A change that is
# meant to change something will differ, and that is the answer, not a failure.
set -eu

REF="${1:-HEAD}"
ROOT=$(cd "$(dirname "$0")/.." && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

build_and_render() {   # <srcdir> <out.wav>
    # Errors go to a log rather than to /dev/null. Silencing these cost a
    # debugging round: the copied tree was missing a file, cmake said so, and
    # the script threw the sentence away and stopped with no output at all.
    if ! cmake -S "$1" -B "$1/_b" -DCMAKE_BUILD_TYPE=Release >"$WORK/log" 2>&1; then
        echo; echo "configure failed:"; tail -20 "$WORK/log"; exit 2
    fi
    if ! cmake --build "$1/_b" --target pd_render -j4 >>"$WORK/log" 2>&1; then
        echo; echo "build failed:"; tail -20 "$WORK/log"; exit 2
    fi
    "$1/_b/pd_render" "$2" >/dev/null 2>&1
}

printf 'rendering %s ... ' "$REF"
git -C "$ROOT" worktree add --detach "$WORK/old" "$REF" >/dev/null 2>&1
build_and_render "$WORK/old" "$WORK/old.wav"
echo done

printf 'rendering the working tree ... '
mkdir -p "$WORK/new"
# Tracked files, plus everything not yet committed. Untracked files matter as
# much as edited ones: a new source file the build needs is invisible to
# ls-files and to diff, and leaving it out makes the copy fail to configure.
{
    git -C "$ROOT" ls-files -z
    git -C "$ROOT" ls-files -z --others --exclude-standard
} | (cd "$ROOT" && xargs -0 tar cf -) | (cd "$WORK/new" && tar xf -)
for f in $(git -C "$ROOT" diff --name-only; git -C "$ROOT" diff --cached --name-only); do
    if [ -f "$ROOT/$f" ]; then
        mkdir -p "$WORK/new/$(dirname "$f")"
        cp "$ROOT/$f" "$WORK/new/$f"
    fi
done
build_and_render "$WORK/new" "$WORK/new.wav"
echo done

git -C "$ROOT" worktree remove --force "$WORK/old" >/dev/null 2>&1 || true

echo
if cmp -s "$WORK/old.wav" "$WORK/new.wav"; then
    echo "identical: not one sample moved since $REF"
    exit 0
fi
echo "different from $REF:"
OLD=$(cksum < "$WORK/old.wav" | cut -d' ' -f1)
NEW=$(cksum < "$WORK/new.wav" | cut -d' ' -f1)
echo "  $REF            $OLD"
echo "  working tree    $NEW"
echo
echo "If this change was meant to be inaudible, that is a regression."
exit 1
