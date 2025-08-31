#!/usr/bin/env bash
set -e  # Exit on any error
rm -f ./build/tri

BUILD_FOLDER="build"
SRC_FOLDER="src"

# Create build folder if it doesn't exist
mkdir -p "$BUILD_FOLDER"

# List C and C++ source files separately
c_src_files=(
    "$SRC_FOLDER/main.c"
    "$SRC_FOLDER/ext.c"
    "$SRC_FOLDER/initialise.c"
    "$SRC_FOLDER/helpers.c"
    "external/SPIRV-Reflect/spirv_reflect.c"
)

cpp_src_files=(
    "$SRC_FOLDER/vma.cpp"
)

# Flags
CFLAGS="-D_DEBUG -DVK_USE_PLATFORM_WAYLAND_KHR -std=c99 -I VulkanMemoryAllocator/include"
CXXFLAGS="-D_DEBUG -DVK_USE_PLATFORM_WAYLAND_KHR -std=c++17 -I VulkanMemoryAllocator/include"
LDFLAGS="-lvulkan -lm -lglfw -lpthread -ldl"

# Output binary
OUTPUT="$BUILD_FOLDER/tri"

# Compile each C source to object
objects=()
for src in "${c_src_files[@]}"; do
    obj="$BUILD_FOLDER/$(basename "${src%.*}").o"
    gcc -c "$src" $CFLAGS -o "$obj"
    objects+=("$obj")
done

# Compile each C++ source to object
for src in "${cpp_src_files[@]}"; do
    obj="$BUILD_FOLDER/$(basename "${src%.*}").o"
    g++ -c "$src" $CXXFLAGS -o "$obj"
    objects+=("$obj")
done

# Link with g++ (to satisfy any C++ runtime needs)
g++ "${objects[@]}" $LDFLAGS -o "$OUTPUT"

echo "Build complete → $OUTPUT"

"$OUTPUT"
