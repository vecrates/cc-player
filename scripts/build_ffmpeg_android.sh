#!/bin/bash
# Build FFmpeg for Android using ffmpeg-kit
# Prerequisites: Android NDK + SDK
#
# Usage:
#   ./scripts/build_ffmpeg_android.sh          # build arm64-v8a + x86_64
#   ./scripts/build_ffmpeg_android.sh arm64    # build single ABI

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/third_party/android"
FKIT_DIR="$PROJECT_DIR/third_party/ffmpeg-kit"

# Resolve SDK/NDK paths
export ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-/Users/jojo/Library/Android/sdk}"
if [ -z "$ANDROID_NDK_ROOT" ]; then
    NDK_DIR=$(ls -d "$ANDROID_SDK_ROOT/ndk/"[0-9]* 2>/dev/null \
        | while read d; do echo "$(basename "$d") $d"; done \
        | sort -t. -k1,1n -k2,2n -k3,3n \
        | tail -1 | cut -d' ' -f2-)
    [ -n "$NDK_DIR" ] && export ANDROID_NDK_ROOT="$NDK_DIR"
fi
export ANDROID_NDK_HOME="${ANDROID_NDK_ROOT}"

if [ -z "$ANDROID_NDK_ROOT" ]; then
    echo "ERROR: Cannot find Android NDK."
    exit 1
fi
echo "NDK: $ANDROID_NDK_ROOT"

# Clone ffmpeg-kit if needed
if [ ! -d "$FKIT_DIR" ]; then
    echo "Cloning ffmpeg-kit..."
    git clone --depth 1 https://github.com/arthenica/ffmpeg-kit.git "$FKIT_DIR"
fi

# Determine which ABIs to build
REQUESTED=("$@")
if [ ${#REQUESTED[@]} -eq 0 ]; then
    REQUESTED=("arm64-v8a" "x86_64")
fi

# All supported ABIs and their disable flags
declare -A DISABLE_FLAGS=(
    ["arm64-v8a"]="--disable-arm-v7a --disable-arm-v7a-neon --disable-x86 --disable-x86-64"
    ["x86_64"]="--disable-arm-v7a --disable-arm-v7a-neon --disable-arm64-v8a --disable-x86"
    ["armeabi-v7a"]="--disable-arm64-v8a --disable-x86 --disable-x86-64"
)

# ffmpeg-kit output arch name per ABI
declare -A FK_ARCH=(
    ["arm64-v8a"]="arm64"
    ["x86_64"]="x86-64"
    ["armeabi-v7a"]="arm"
)

for ABI in "${REQUESTED[@]}"; do
    DISABLE=${DISABLE_FLAGS[$ABI]}
    ARCH=${FK_ARCH[$ABI]}

    if [ -z "$DISABLE" ]; then
        echo "WARNING: Unsupported ABI '$ABI', skipping."
        continue
    fi

    echo ""
    echo "=== Building FFmpeg for $ABI ==="
    cd "$FKIT_DIR"

    # shellcheck disable=SC2086
    ./android.sh \
        --enable-gpl \
        --no-archive \
        $DISABLE

    # ffmpeg-kit outputs to: prebuilt/android-<arch>/
    FK_PREBUILT="$FKIT_DIR/prebuilt/android-${ARCH}"

    if [ ! -d "$FK_PREBUILT" ]; then
        echo "ERROR: Output not found at $FK_PREBUILT"
        ls -la "$FKIT_DIR/prebuilt/" 2>/dev/null || echo "No prebuilt/ directory"
        exit 1
    fi

    # Copy to per-ABI output directory
    DEST="$OUTPUT_DIR/$ABI"
    rm -rf "$DEST"
    mkdir -p "$DEST"
    cp -r "$FK_PREBUILT/ffmpeg/include" "$DEST/"
    mkdir -p "$DEST/lib"
    cp "$FK_PREBUILT/ffmpeg/lib/"*.so "$DEST/lib/" 2>/dev/null || true

    echo "Installed to $DEST"
    echo "  Headers: $(ls "$DEST/include/" | wc -l | tr -d ' ') entries"
    echo "  Libs: $(ls "$DEST/lib/"*.so 2>/dev/null | xargs -n1 basename 2>/dev/null | tr '\n' ' ')"
done

echo ""
echo "=== Done ==="
for d in "$OUTPUT_DIR"/*/; do
    [ -d "$d" ] || continue
    echo "  $(basename "$d"): $(ls "$d/lib/"*.so 2>/dev/null | wc -l | tr -d ' ') shared libs"
done
