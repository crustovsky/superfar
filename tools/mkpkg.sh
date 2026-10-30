#!/bin/sh
# Builds the pacman package from this checkout rather than from a release
# tarball, so it works before a tag exists and while the repo is private.
# The result lands in packaging/aur as superfar-<version>-<rel>-<arch>.pkg.tar.zst.
set -eu
cd "$(dirname "$0")/.."
ver=$(sed -n 's/^pkgver=//p' packaging/aur/PKGBUILD)
git archive --format=tar.gz --prefix="superfar-$ver/" -o "packaging/aur/superfar-$ver.tar.gz" HEAD
cd packaging/aur
makepkg --noconfirm --skipchecksums --force --cleanbuild "$@"
