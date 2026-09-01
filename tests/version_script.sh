#!/usr/bin/env bash
# =============================================================================
# Does scripts/version.sh number a build the way a release expects?
#
# The one property everything else rests on: a build made AT a tag reports .0.
# If that is wrong, the released executable claims to be some commits past the
# previous release, the website disagrees with the download, and the number
# stops being worth reading -- which costs more than having no number at all.
#
# Run directly:  bash tests/version_script.sh
#
# Shell rather than C++ because the logic under test is shell. It builds real
# throwaway repositories rather than mocking git, because every interesting
# case here (a tag with no commits after it, a shallow clone with no tags) is a
# fact about git and a mock would only assert my assumptions back at me.
# =============================================================================
set -u

SELF=$(cd "$(dirname "$0")/.." && pwd)
SCRIPT="$SELF/scripts/version.sh"
fails=0

check() {
    local what="$1" got="$2" want="$3"
    if [ "$got" = "$want" ]; then
        printf '  ok   %-44s %s\n' "$what" "$got"
    else
        printf '  FAIL %-44s got %s, want %s\n' "$what" "$got" "$want"
        fails=$((fails + 1))
    fi
}

sandbox() {
    local dir
    dir=$(mktemp -d)
    mkdir -p "$dir/scripts" "$dir/src"
    cp "$SCRIPT" "$dir/scripts/version.sh"
    git -C "$dir" init -q
    git -C "$dir" config user.email t@example.com
    git -C "$dir" config user.name Test
    echo "$dir"
}

commit() {   # commit <dir> <message>
    git -C "$1" add -A
    git -C "$1" -c commit.gpgsign=false commit -q -m "$2"
}

# --- an untagged repository cannot know, and must say so --------------------
D=$(sandbox)
echo "first" > "$D/src/a.cpp"
commit "$D" "first"
check "no tags at all" "$(bash "$D/scripts/version.sh")" "unknown"

# --- at the tag: the number a release ships with ----------------------------
git -C "$D" tag v1.2.3
check "sitting exactly on the tag" "$(bash "$D/scripts/version.sh")" "1.2.3.0"

# --- and it tickers from there ----------------------------------------------
echo "second" > "$D/src/b.cpp"
commit "$D" "second"
check "one commit after the tag" "$(bash "$D/scripts/version.sh")" "1.2.3.1"

echo "third" > "$D/src/c.cpp"
commit "$D" "third"
check "two commits after the tag" "$(bash "$D/scripts/version.sh")" "1.2.3.2"

# --- a newer tag restarts the count -----------------------------------------
git -C "$D" tag v1.3.0
check "a fresh tag resets the ticker" "$(bash "$D/scripts/version.sh")" "1.3.0.0"

# --- uncommitted source is not the build the tag describes ------------------
echo "scratch" >> "$D/src/a.cpp"
check "edited source is marked" "$(bash "$D/scripts/version.sh")" "1.3.0.0+dirty"
git -C "$D" checkout -- src/a.cpp

# --- but prose is not source -------------------------------------------------
# Marking every README edit would mean a local build almost never matches the
# published one even when the compiled code is identical, and a marker that is
# always on tells you nothing.
echo "notes" > "$D/README.md"
check "an untracked README is not dirty" "$(bash "$D/scripts/version.sh")" "1.3.0.0"

rm -rf "$D"

if [ "$fails" -eq 0 ]; then echo "PASS"; else echo "$fails version check(s) failed"; fi
exit $((fails > 0))
