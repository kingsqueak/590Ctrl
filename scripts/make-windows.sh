#!/usr/bin/env bash
# Build the Windows zip of 590Ctrl: the program with its Qt, nothing to install.
# Run from Git Bash with Visual Studio 2022 and the official Qt.
# Usage: scripts/make-windows.sh <path to Qt, e.g. C:/Qt/6.12.0/msvc2022_64>
# Writes dist/590Ctrl-<version>-win-x64.zip
set -euo pipefail
qt=${1:?path to the official Qt}
cd "$(dirname "$0")/.."

version=$(sed -n 's/^project(590Ctrl VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="$qt"
cmake --build build-win --config Release
# The tests need Qt's DLLs, and have no display here.
PATH="$(cygpath "$qt")/bin:$PATH" QT_QPA_PLATFORM=offscreen ctest --test-dir build-win -C Release --output-on-failure

stage="build-win/stage/590Ctrl-$version"
rm -rf build-win/stage
cmake --install build-win --config Release --prefix "$stage"
cp LICENSE "$stage/"
# A fresh Windows may not have the Visual C++ runtime; Microsoft allows shipping it.
for dll in vcruntime140.dll vcruntime140_1.dll msvcp140.dll; do
  [ -e "$stage/bin/$dll" ] || cp "/c/Windows/System32/$dll" "$stage/bin/"
done
mkdir -p dist
out="$PWD/dist/590Ctrl-$version-win-x64.zip"
rm -f "$out"
(cd build-win/stage && 7z a -tzip -bso0 "$out" "590Ctrl-$version")
ls -la dist/
