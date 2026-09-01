#!/usr/bin/env bash
# =============================================================================
# version.sh -- what version is this working tree?
#
# Prints one line, e.g. "0.4.5.3", for the build scripts to compile in and for
# the menu and the web page to display.
#
#     MAJOR.MINOR.PATCH  the most recent release tag
#     .N                 commits made since that tag
#
# The point of the fourth number is to answer "is the thing in front of me
# actually the change I just pushed?" -- a question a release version cannot
# answer, because it only moves a few times a month while the page rebuilds on
# every push.
#
# --- why nothing is stored -------------------------------------------------
# The obvious way to do this is a counter in a file that a commit hook bumps,
# and it does not work: the counter is itself committed, so bumping it is a
# commit, which bumps the counter. Worse, the file is a merge conflict on every
# branch that touches it, and a rebase silently makes it wrong.
#
# There is no counter here. Git already knows how many commits are on top of a
# tag, and asking it is always right, needs no hook, no pipeline and no
# discipline. It cannot drift from the repository because it IS the repository.
#
# This also settles the ordering problem -- that a release is decided after the
# executable exists. It is not, any more: tagging is the decision, and
# .github/workflows/release.yml builds the released executable FROM the tag, on
# a checkout where `git rev-list --count v0.4.5..HEAD` is 0. So the shipped
# build says 0.4.5.0 without anyone editing anything. A build made locally
# before tagging honestly reports itself as some number of commits past the
# PREVIOUS release, which is what it is.
#
# --- the failure that matters ----------------------------------------------
# `git describe` needs tags, and `actions/checkout` does not fetch them unless
# asked. Both workflows therefore set fetch-depth: 0. Without it this prints
# "unknown" in CI and the website shows no version at all -- which is the
# reason for printing "unknown" rather than inventing a plausible 0.0.0. A
# version number that is wrong is worse than one that is absent, because the
# whole purpose of this is to be trusted when checking whether a deploy landed.
# =============================================================================
set -u

cd "$(dirname "$0")/.."

tag=$(git describe --tags --abbrev=0 2>/dev/null || true)
[ -n "$tag" ] || { echo "unknown"; exit 0; }

count=$(git rev-list --count "$tag"..HEAD 2>/dev/null || true)
[ -n "$count" ] || { echo "unknown"; exit 0; }

version="${tag#v}.$count"

# Only sources that end up IN the binary count as dirty. A modified README does
# not make the build something other than what the tag says it is, and marking
# it would mean the local build almost never matches the site even when the
# code is identical.
#
# core.fileMode=false because the question is whether the CODE differs from
# the tag, and a permission bit is not code. This is not hypothetical: the
# Pages workflow used to chmod +x this script before running it, git counted
# that mode change as a modification, and the website published itself as
# "0.4.4.19+dirty" from a checkout that was untouched. The workflow no longer
# chmods, but the check should not have been fooled by it either -- a build
# on any filesystem that reports modes differently would have hit the same
# thing.
if [ -n "$(git -c core.fileMode=false status --porcelain -- src Makefile build.bat build_web.sh 2>/dev/null)" ]; then
    version="$version+dirty"
fi

echo "$version"
