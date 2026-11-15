#!/bin/sh
# Downloads ONNX Runtime (MIT), which runs the DeepCW decoder's model, into
# third_party/onnxruntime: headers to build with and the library to ship.
# CMake picks it up from there. Without it the app builds with no DeepCW.
#
# Usage: scripts/fetch-onnxruntime.sh [osx-arm64 | linux-x64 | linux-aarch64]
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
        *) echo "No ONNX Runtime download for $(uname -s) $(uname -m)" >&2; exit 1 ;;
    esac
fi
case "$PLATFORM" in
    osx-arm64)     SHA256=1031b36dd3bdaa8d976a6130d9cf2a05e9be320b93ae804f8427b91dc3432afc ;;
    *) SHA256= ;;
esac

NAME=onnxruntime-$PLATFORM-$VERSION
DEST=third_party/onnxruntime
if [ -f "$DEST/VERSION_NUMBER" ] && [ "$(cat "$DEST/VERSION_NUMBER")" = "$VERSION" ] && [ -f "$DEST/.platform-$PLATFORM" ]; then
    echo "ONNX Runtime $VERSION ($PLATFORM) is already in $DEST"
    exit 0
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
curl -fsSL -o "$TMP/$NAME.tgz" "https://github.com/microsoft/onnxruntime/releases/download/v$VERSION/$NAME.tgz"
if [ -n "$SHA256" ]; then
    echo "$SHA256  $TMP/$NAME.tgz" | shasum -a 256 -c - >/dev/null || { echo "Checksum mismatch for $NAME.tgz" >&2; exit 1; }
fi
tar -xzf "$TMP/$NAME.tgz" -C "$TMP"
rm -rf "$DEST"
mkdir -p third_party
mv "$TMP/$NAME" "$DEST"
touch "$DEST/.platform-$PLATFORM"
echo "ONNX Runtime $VERSION ($PLATFORM) is in $DEST"
