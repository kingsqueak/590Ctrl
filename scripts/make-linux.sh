#!/bin/sh
# Build the RPM and Arch packages of 590Ctrl for this machine's architecture.
# Unlike the deb, these carry their own Qt, so they don't depend on which Qt a
# distribution ships. Needs fpm (gem install fpm) and the official Qt.
#
# Usage: scripts/make-linux.sh <path to Qt, e.g. ~/Qt/6.12.0/gcc_64>
# Writes dist/590ctrl-<version>-1.<arch>.rpm and a .pkg.tar.zst
set -eu
QT=${1:?path to the official Qt}
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(590Ctrl VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCH=$(uname -m)
[ -n "$VERSION" ] || { echo "Could not read the project version" >&2; exit 1; }

cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
      -DCMAKE_PREFIX_PATH="$QT" -DCMAKE_INSTALL_PREFIX=/usr/lib/590ctrl
cmake --build build-linux

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
DESTDIR="$STAGE" cmake --install build-linux
strip --strip-unneeded "$STAGE/usr/lib/590ctrl/bin/590Ctrl"
for need in lib/libQt6Core.so.6 plugins/platforms/libqxcb.so plugins/multimedia/libffmpegmediaplugin.so; do
    [ -e "$STAGE/usr/lib/590ctrl/$need" ] || { echo "missing $need" >&2; exit 1; }
done

mkdir -p "$STAGE/usr/bin" "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/512x512/apps" "$STAGE/usr/share/licenses/590ctrl"
ln -s ../lib/590ctrl/bin/590Ctrl "$STAGE/usr/bin/590Ctrl"
install -m 0644 resources/590Ctrl.png "$STAGE/usr/share/icons/hicolor/512x512/apps/590ctrl.png"
install -m 0644 LICENSE "$STAGE/usr/share/licenses/590ctrl/LICENSE"
cat > "$STAGE/usr/share/applications/590ctrl.desktop" << 'DESKTOP'
[Desktop Entry]
Type=Application
Name=590Ctrl
GenericName=Kenwood TS-590 control
Comment=Control a Kenwood TS-590 over USB CAT, and serve it over TCI
Exec=590Ctrl
Icon=590ctrl
Terminal=false
Categories=HamRadio;Network;
Keywords=kenwood;ts-590;cat;tci;radio;
DESKTOP

common() {
    fpm -s dir -C "$STAGE" -n 590ctrl -v "$VERSION" --iteration 1 -a "$ARCH" \
        --license GPL-3.0-or-later --url https://github.com/kingsqueak/590Ctrl \
        --maintainer "KC2RGW Chris <kc2rgw@radiopathology.com>" \
        --description "Kenwood TS-590SG control panel" "$@" usr
}
mkdir -p dist
rm -f dist/*.rpm dist/*.pkg.tar.zst
common -t rpm -p "dist/590ctrl-${VERSION}-1.${ARCH}.rpm" \
    -d glibc -d libstdc++ -d libgcc -d libglvnd-egl -d libglvnd-opengl -d libxkbcommon-x11 -d fontconfig \
    -d freetype -d dbus-libs -d xcb-util-cursor -d xcb-util-wm -d xcb-util-image -d xcb-util-keysyms \
    -d xcb-util-renderutil -d pulseaudio-libs
common -t pacman -p "dist/590ctrl-${VERSION}-1-${ARCH}.pkg.tar.zst" \
    -d glibc -d gcc-libs -d libglvnd -d libxkbcommon-x11 -d fontconfig -d freetype2 -d dbus \
    -d xcb-util-cursor -d xcb-util-wm -d xcb-util-image -d xcb-util-keysyms -d xcb-util-renderutil -d libpulse
ls -la dist/
