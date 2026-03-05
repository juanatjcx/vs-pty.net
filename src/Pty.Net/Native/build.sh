#!/bin/bash
# Build script for pty_shim native library
#
# This creates a dynamic library (.dylib) that wraps variadic ioctl calls
# for use with .NET P/Invoke on Apple Silicon (ARM64).

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Detect architecture
ARCH=$(uname -m)
echo "Building pty_shim for architecture: $ARCH"

# Output directory (relative to Pty.Net project)
OUTPUT_DIR="$SCRIPT_DIR/../../../bin/Pty.Net/Debug/netstandard2.0/runtimes/osx/native"
mkdir -p "$OUTPUT_DIR"

# Compile the shim
# -dynamiclib: Create a dynamic library
# -arch: Target architecture (arm64 for Apple Silicon, x86_64 for Intel)
# -install_name: The library's install name
# -o: Output file

if [ "$ARCH" = "arm64" ]; then
    echo "Compiling for ARM64 (Apple Silicon)..."
    clang -dynamiclib \
        -arch arm64 \
        -install_name @rpath/libpty_shim.dylib \
        -o "$OUTPUT_DIR/libpty_shim.dylib" \
        pty_shim.c
elif [ "$ARCH" = "x86_64" ]; then
    echo "Compiling for x86_64 (Intel)..."
    clang -dynamiclib \
        -arch x86_64 \
        -install_name @rpath/libpty_shim.dylib \
        -o "$OUTPUT_DIR/libpty_shim.dylib" \
        pty_shim.c
else
    echo "Unknown architecture: $ARCH"
    exit 1
fi

echo "Built: $OUTPUT_DIR/libpty_shim.dylib"

# Also copy to the catra-cli output directory if the CLI bin directory exists
CLI_BIN_DIR="$SCRIPT_DIR/../../../../../catra-cli/src/bin/Debug/net10.0"
if [ -d "$CLI_BIN_DIR" ]; then
    CLI_OUTPUT_DIR="$CLI_BIN_DIR/runtimes/osx/native"
    mkdir -p "$CLI_OUTPUT_DIR"
    cp "$OUTPUT_DIR/libpty_shim.dylib" "$CLI_OUTPUT_DIR/"
    echo "Copied to: $CLI_OUTPUT_DIR/libpty_shim.dylib"
fi

echo "Done!"
