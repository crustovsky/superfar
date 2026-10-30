#!/bin/sh
# Cuts a release: bumps the version in CMakeLists.txt (the one source; the
# binary and the tarball read it) and the PKGBUILD, commits and tags
# vX.Y.Z. Then `git push --follow-tags`: the Release workflow builds the
# pacman package and the tarball and attaches both to a GitHub release.
set -eu
ver="${1:?usage: tools/release.sh X.Y.Z}"
cd "$(dirname "$0")/.."
[ "$(git branch --show-current)" = main ] || { echo "not on main" >&2; exit 1; }
[ -z "$(git status --porcelain)" ] || { echo "working tree not clean" >&2; exit 1; }
[ -z "$(git tag -l "v$ver")" ] || { echo "v$ver already exists" >&2; exit 1; }
sed -i "s/^project(superfar VERSION [0-9.]*/project(superfar VERSION $ver/" CMakeLists.txt
sed -i "s/^pkgver=.*/pkgver=$ver/; s/^pkgrel=.*/pkgrel=1/" packaging/aur/PKGBUILD
git add CMakeLists.txt packaging/aur/PKGBUILD
git commit -q -m "Release v$ver"
git tag -a "v$ver" -m "superfar v$ver"
echo "tagged v$ver; now: git push --follow-tags"
