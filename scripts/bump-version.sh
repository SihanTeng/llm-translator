#!/usr/bin/env bash
# Bump the project version, commit, tag, and push — the tag triggers the
# release workflow (AppImage + RPM + tarball) on GitHub.
#
# Usage: scripts/bump-version.sh <major|minor|patch|X.Y.Z>
#
# Source of truth for the current version is the latest v* git tag; the
# CMakeLists project() version is updated to match. The release workflow
# embeds the tag's version into artifacts, so the two never drift.

set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
cd "$ROOT"

usage() {
    echo "usage: $0 <major|minor|patch|X.Y.Z>" >&2
    exit 2
}

[ $# -eq 1 ] || usage

git diff --quiet && git diff --cached --quiet || {
    echo "error: uncommitted changes — commit or stash first" >&2
    exit 1
}

latest=$(git tag -l 'v*' --sort=-v:refname | head -1)
current="${latest#v}"
if [ -z "$current" ]; then
    current=$(grep -oP 'project\(translator VERSION \K[0-9.]+' CMakeLists.txt) || true
fi
[ -n "$current" ] || { echo "error: cannot determine current version" >&2; exit 1; }

IFS=. read -r major minor patch <<< "$current"
patch=${patch:-0}
case "$1" in
    major) new="$((major + 1)).0.0" ;;
    minor) new="$major.$((minor + 1)).0" ;;
    patch) new="$major.$minor.$((patch + 1))" ;;
    *.*.*) new="$1" ;;
    *) usage ;;
esac

[ "$new" != "$current" ] || { echo "error: version $new is not a bump" >&2; exit 1; }

sed -i -E "s/project\(translator VERSION [0-9.]+\)/project(translator VERSION $new)/" CMakeLists.txt
grep -q "project(translator VERSION $new)" CMakeLists.txt || {
    echo "error: failed to update CMakeLists.txt" >&2
    exit 1
}

git add CMakeLists.txt
git commit -m "Bump version to $new"
git tag -a "v$new" -m "v$new"
git push origin main "v$new"

echo "Released v$new — the release workflow is now building artifacts."
