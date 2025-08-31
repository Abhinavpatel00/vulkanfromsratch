#!/usr/bin/env bash
set -e  # Exit on any error
rm ./build/tri

BUILD_FOLDER="build"
SRC_FOLDER="src"

# Create build folder if it doesn't exist
mkdir -p "$BUILD_FOLDER"

# List all source files
src_files=(
    "$SRC_FOLDER/main.c"
    "$SRC_FOLDER/ext.c"
    "$SRC_FOLDER/initialise.c"
    "$SRC_FOLDER/helpers.c"
    "external/SPIRV-Reflect/spirv_reflect.c"
)

# Compiler flags
CFLAGS="-D_DEBUG -DVK_USE_PLATFORM_WAYLAND_KHR -std=c99"
LDFLAGS="-lvulkan -lm -lglfw -lpthread -ldl"

# Output binary
OUTPUT="$BUILD_FOLDER/tri"

# Compile all sources into the final binary
gcc "${src_files[@]}" $CFLAGS $LDFLAGS -o "$OUTPUT"

echo "Build complete → $OUTPUT"

./build/tri
