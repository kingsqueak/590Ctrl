#!/bin/sh
# Build a .deb of 590Ctrl for the Ubuntu this script is running on.
# The package links that release's Qt 6 and names the libraries apt must
# install. Run it on Ubuntu 26.04 to match the published arm64 package.
#
# Writes dist/590ctrl_<version>-1_<arch>.deb
set -eu
cd "$(dirname "$0")/.."

VERSION=$(sed -n 's/^project(590Ctrl VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
ARCH=$(dpkg --print-architecture)
[ -n "$VERSION" ] || { echo "Could not read the project version" >&2; exit 1; }

cmake -S . -B build-deb -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-deb
# The runner has no display. offscreen is enough for the unit tests.
QT_QPA_PLATFORM=offscreen ctest --test-dir build-deb --output-on-failure

STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/DEBIAN" \
    "$STAGE/usr/bin" \
    "$STAGE/usr/share/applications" \
    "$STAGE/usr/share/icons/hicolor/512x512/apps" \
    "$STAGE/usr/share/doc/590ctrl"

install -m 0755 build-deb/590Ctrl "$STAGE/usr/bin/590Ctrl"
strip --strip-unneeded "$STAGE/usr/bin/590Ctrl"
install -m 0644 resources/590Ctrl.png \
    "$STAGE/usr/share/icons/hicolor/512x512/apps/590ctrl.png"

cat > "$STAGE/usr/share/applications/590ctrl.desktop" << 'EOF'
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
EOF

cat > "$STAGE/usr/share/doc/590ctrl/copyright" << 'EOF'
Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: 590Ctrl
Upstream-Contact: KC2RGW Chris <kc2rgw@gmail.com>
Source: https://github.com/kingsqueak/590Ctrl

Files: *
Copyright: 2026 KC2RGW Chris Esser
License: GPL-3+
 On Debian systems, the full text of the GNU General Public License
 version 3 can be found in /usr/share/common-licenses/GPL-3.
EOF

cat > "$STAGE/usr/share/doc/590ctrl/README.Debian" << 'EOF'
590Ctrl uses the serial device the radio enumerates, usually
/dev/ttyUSB0. That node is group dialout. Add your user to the group
and log in again:

  sudo usermod -aG dialout "$USER"

The package links the Qt 6 libraries shipped with this Ubuntu release,
so apt installs those when you install this deb.
EOF

gzip -n -9 -c > "$STAGE/usr/share/doc/590ctrl/changelog.Debian.gz" << EOF
590ctrl (${VERSION}-1) resolute; urgency=medium

  * Binary package of 590Ctrl ${VERSION}.

 -- KC2RGW Chris <kc2rgw@gmail.com>  $(date -R)
EOF

find "$STAGE" -type d -exec chmod 0755 {} +
find "$STAGE" -type f -exec chmod a-x,go-w {} +
chmod 0755 "$STAGE/usr/bin/590Ctrl"

SHLIB=$(mktemp -d)
mkdir -p "$SHLIB/debian"
printf 'Source: 590ctrl\n\nPackage: 590ctrl\nArchitecture: %s\n' "$ARCH" > "$SHLIB/debian/control"
DEPS=$(cd "$SHLIB" && dpkg-shlibdeps -O "$STAGE/usr/bin/590Ctrl" | sed -n 's/^shlibs:Depends=//p')
rm -rf "$SHLIB"
# The xcb cursor library and the Wayland platform plugin are loaded by
# name at startup, so they never show up as NEEDED entries.
DEPS="$DEPS, libqt6waylandclient6, libxcb-cursor0"

SIZE=$(du -sk "$STAGE/usr" | awk '{print $1}')
cat > "$STAGE/DEBIAN/control" << EOF
Package: 590ctrl
Version: ${VERSION}-1
Section: hamradio
Priority: optional
Architecture: ${ARCH}
Maintainer: KC2RGW Chris <kc2rgw@gmail.com>
Homepage: https://github.com/kingsqueak/590Ctrl
Installed-Size: ${SIZE}
Depends: ${DEPS}
Recommends: qt6-gtk-platformtheme, qt6-qpa-plugins
Description: Kenwood TS-590SG control panel
 Qt 6 panel for a Kenwood TS-590SG over USB CAT. It also serves TCI,
 so programs such as WSJT-X, fldigi and an RF amplifier can use the
 radio without opening the serial port themselves.
 .
 Built against this Ubuntu release's Qt 6. apt installs the libraries.
 The user who operates the radio needs to be in the dialout group.
EOF

mkdir -p dist
OUT="dist/590ctrl_${VERSION}-1_${ARCH}.deb"
rm -f "$OUT"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT"
echo "Wrote $OUT"
sha256sum "$OUT"
