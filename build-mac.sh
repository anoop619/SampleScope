#!/usr/bin/env bash
# Builds SampleScope.vst3 (universal: Apple Silicon + Intel) and installs it for your user.
#   ./build-mac.sh            build + install to ~/Library/Audio/Plug-Ins/VST3
#   ./build-mac.sh --test     also build and run the test suite first
set -euo pipefail
cd "$(dirname "$0")"

RUN_TESTS=OFF
[[ "${1:-}" == "--test" ]] && RUN_TESTS=ON

if ! xcode-select -p >/dev/null 2>&1; then
    echo "Xcode command line tools are missing. Installing them (a dialog will appear), then re-run this script."
    xcode-select --install || true
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    if command -v brew >/dev/null 2>&1; then
        echo "Installing CMake with Homebrew..."
        brew install cmake
    else
        echo "CMake is required: install it from https://cmake.org/download/ or with Homebrew (brew install cmake)."
        exit 1
    fi
fi

GENERATOR=()
command -v ninja >/dev/null 2>&1 && GENERATOR=(-G Ninja)

echo "==> Configuring (first run downloads JUCE and Signalsmith Stretch, ~1-2 min)"
cmake -S . -B build ${GENERATOR[@]+"${GENERATOR[@]}"} -DCMAKE_BUILD_TYPE=Release -DSAMPLESCOPE_TESTS=${RUN_TESTS}

if [[ "$RUN_TESTS" == "ON" ]]; then
    echo "==> Building and running tests"
    cmake --build build --config Release --target SampleScopeTests -j "$(sysctl -n hw.ncpu)"
    TEST_BIN=$(find build -type f -name SampleScopeTests -perm +111 | head -1)
    "$TEST_BIN"
fi

echo "==> Building the VST3"
cmake --build build --config Release --target SampleScope_VST3 -j "$(sysctl -n hw.ncpu)"

PLUGIN=$(find build -type d -name "SampleScope.vst3" -path "*Release*" | head -1)
DEST="$HOME/Library/Audio/Plug-Ins/VST3"
mkdir -p "$DEST"
rm -rf "$DEST/SampleScope.vst3"
cp -R "$PLUGIN" "$DEST/"
codesign --force --deep --sign - "$DEST/SampleScope.vst3" >/dev/null 2>&1 || true

echo
echo "Installed: $DEST/SampleScope.vst3"
echo "In Ableton Live: Settings > Plug-Ins > make sure 'Use VST3 Plug-In System Folders' is on, then Rescan."
echo "Then drop SampleScope on your Master track."
