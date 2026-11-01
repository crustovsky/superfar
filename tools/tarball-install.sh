#!/bin/sh
# Copies this tree into a prefix, ~/.local by default: ./install.sh [prefix]
set -eu
prefix="${1:-$HOME/.local}"
cd "$(dirname "$0")"
mkdir -p "$prefix"
cp -r bin share "$prefix/"
# The entry names the binary by its full path: a launcher's PATH rarely
# has the prefix on it.
sed -i "s|^Exec=.*|Exec=$prefix/bin/superfar %U|" "$prefix/share/applications/superfar.desktop"
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$prefix/share/applications" || true
fi
echo "superfar installed under $prefix; make sure $prefix/bin is on PATH"
