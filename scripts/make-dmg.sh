#!/bin/sh
# Builds the macOS release: build-release/590Ctrl-<version>.dmg holding
# 590Ctrl-<version>.app and an Applications link. Universal (arm64 + x86_64)
# for macOS 26.0 and later.
#
# Usage: scripts/make-dmg.sh [path to the official Qt, e.g. ~/Qt/6.12.0/macos]
#
# Needs the official Qt binaries (see README): Homebrew's Qt and its
# dependencies are built for a newer macOS than the release supports.
set -eu

QT=${1:-$HOME/Qt/6.12.0/macos}
MIN_MACOS=26.0
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(590Ctrl VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
BUILD=build-release
STAGE=$BUILD/dmg
APP="$STAGE/590Ctrl-$VERSION.app"
DMG="$BUILD/590Ctrl-$VERSION.dmg"

[ -x "$QT/bin/macdeployqt" ] || { echo "No Qt at $QT" >&2; exit 1; }

# Start from a fresh bundle, not one a previous run deployed into. Before
# configuring: that's when CMake writes the bundle's Info.plist.
rm -rf "$BUILD/590Ctrl.app"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DCMAKE_PREFIX_PATH="$QT" \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_MACOS -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build "$BUILD"

# macdeployqt finds the executable by the bundle's name, so deploy first and
# rename after. It exits 0 even when it fails, hence the ERROR check.
"$QT/bin/macdeployqt" "$BUILD/590Ctrl.app" 2>&1 | tee "$BUILD/macdeployqt.log"
if grep -q ERROR "$BUILD/macdeployqt.log"; then
    echo "macdeployqt failed" >&2
    exit 1
fi
# The version goes in the bundle's name so it shows in Finder and /Applications.
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$BUILD/590Ctrl.app" "$APP"
# No Developer ID: sign ad hoc so the bundle's signature is at least intact.
codesign --force --deep --sign - "$APP"
codesign --verify --deep --strict "$APP"

# Every binary must run on the minimum macOS and use only bundled libraries.
failed=0
for f in $(find "$APP" -type f); do
    file "$f" | grep -q Mach-O || continue
    for minos in $(otool -arch all -l "$f" | awk '/minos/ {print $2}'); do
        if [ "$(printf '%s\n%s\n' "$minos" "$MIN_MACOS" | sort -V | tail -1)" != "$MIN_MACOS" ]; then
            echo "needs macOS $minos: $f" >&2
            failed=1
        fi
    done
    if otool -L "$f" | grep -q -e /opt/homebrew -e /usr/local; then
        echo "links outside the bundle: $f" >&2
        failed=1
    fi
done
for needed in Frameworks/QtCore.framework Frameworks/QtNetwork.framework Frameworks/QtSerialPort.framework \
              PlugIns/platforms/libqcocoa.dylib PlugIns/tls; do
    [ -e "$APP/Contents/$needed" ] || { echo "missing $needed" >&2; failed=1; }
done
lipo -archs "$APP/Contents/MacOS/590Ctrl" | grep -q 'x86_64 arm64\|arm64 x86_64' || {
    echo "not universal" >&2
    failed=1
}
[ "$failed" = 0 ] || exit 1

ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "590Ctrl $VERSION" -srcfolder "$STAGE" -format UDZO "$DMG"
echo "Built $DMG"
