#!/bin/sh
# Downloads ONNX Runtime (MIT), which runs the DeepCW decoder's model, into
# third_party/onnxruntime: headers to build with and the library to ship.
# CMake picks it up from there. Without it the app builds with no DeepCW.
#
# Usage: scripts/fetch-onnxruntime.sh [osx-arm64 | linux-x64 | linux-aarch64 | win-x64]
#
# There's no Intel Mac build: the universal app loads the library at run time
# and offers DeepCW on Apple silicon only.
set -eu

VERSION=1.31.0
cd "$(dirname "$0")/.."

if [ $# -gt 0 ]; then
    PLATFORM=$1
else
    case "$(uname -s)-$(uname -m)" in
        Darwin-*)      PLATFORM=osx-arm64 ;;
        Linux-x86_64)  PLATFORM=linux-x64 ;;
        Linux-aarch64) PLATFORM=linux-aarch64 ;;
        MINGW*-x86_64|MSYS*-x86_64) PLATFORM=win-x64 ;;
        *) echo "No ONNX Runtime download for $(uname -s) $(uname -m)" >&2; exit 1 ;;
    esac
fi
case "$PLATFORM" in
    osx-arm64)     SHA256=1031b36dd3bdaa8d976a6130d9cf2a05e9be320b93ae804f8427b91dc3432afc ;;
    linux-x64)     SHA256=cc5c72baf5ae5c8238a6841f0897227be2d02826b9cf98eaf02fdefaeeb03a57 ;;
    linux-aarch64) SHA256=c5b8b3cca31f3d643a3b313b8f42e2d0f16a4fcefd02cda343d97bd5306afaab ;;
    win-x64)       SHA256=3958d8a44984160692c1b4ffd0de3cb48f8c39f9d34bb31e433db06e30b82195 ;;
    *) echo "No ONNX Runtime download for $PLATFORM" >&2; exit 1 ;;
esac
case "$PLATFORM" in
    win-*) EXT=zip ;;
    *)     EXT=tgz ;;
esac

NAME=onnxruntime-$PLATFORM-$VERSION
DEST=third_party/onnxruntime
if [ -f "$DEST/VERSION_NUMBER" ] && [ "$(cat "$DEST/VERSION_NUMBER")" = "$VERSION" ] && [ -f "$DEST/.platform-$PLATFORM" ]; then
    echo "ONNX Runtime $VERSION ($PLATFORM) is already in $DEST"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/$NAME.$EXT" "https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$NAME.$EXT"
echo "$SHA256  $TMP/$NAME.$EXT" | shasum -a 256 -c - >/dev/null || { echo "Checksum mismatch for $NAME.$EXT" >&2; exit 1; }
if [ "$EXT" = zip ]; then
    unzip -q "$TMP/$NAME.zip" -d "$TMP"
else
    tar -xzf "$TMP/$NAME.tgz" -C "$TMP"
fi
rm -rf "$DEST"
mkdir -p third_party
mv "$TMP/$NAME" "$DEST"
touch "$DEST/.platform-$PLATFORM"
echo "ONNX Runtime $VERSION ($PLATFORM) is in $DEST"
