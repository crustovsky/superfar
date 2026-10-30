#!/bin/sh
# Builds a binary tarball from this checkout: the installed tree under a
# relocatable prefix plus LICENSE, README and an install script. Linked
# against the libraries of the machine that builds it (Arch in CI), so it
# needs the same generation: Qt 6.12, glib 2.90, glycin 2 or newer.
set -eu
cd "$(dirname "$0")/.."
ver=$(sed -n 's/^project(superfar VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
name="superfar-v$ver-linux-$(uname -m)"
rm -rf "dist/$name" build-dist
cmake -S . -B build-dist -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-dist
DESTDIR="$PWD/dist/$name" cmake --install build-dist --prefix / --strip
cp LICENSE README.md "dist/$name/"
cp tools/tarball-install.sh "dist/$name/install.sh"
tar -C dist -czf "dist/$name.tar.gz" "$name"
echo "dist/$name.tar.gz"
