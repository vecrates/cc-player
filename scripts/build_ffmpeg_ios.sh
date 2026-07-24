#!/bin/bash
# Build FFmpeg for iOS using ffmpeg-kit
# Prerequisites: Xcode, ffmpeg-kit source

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
OUTPUT_DIR="$PROJECT_DIR/third_party/ios"

echo "=== Building FFmpeg for iOS ==="

# Clone ffmpeg-kit if not present
if [ ! -d "$PROJECT_DIR/third_party/ffmpeg-kit" ]; then
    echo "Cloning ffmpeg-kit..."
    git clone --depth 1 https://github.com/arthenica/ffmpeg-kit.git \
        "$PROJECT_DIR/third_party/ffmpeg-kit"
fi

cd "$PROJECT_DIR/third_party/ffmpeg-kit"

# Build with min configuration (H.264 + AAC only)
./ffmpeg-kit/ios.sh \
    --min \
    --enable-gpl \
    --disable-armv7 \
    --disable-armv7s \
    --enable-neon

echo "=== iOS FFmpeg build complete ==="
echo "Output: $OUTPUT_DIR"
