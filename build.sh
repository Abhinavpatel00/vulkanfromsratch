#!/usr/bin/env bash
set -e  # Exit on any error
rm -f ./build/tri

BUILD_FOLDER="build"
SRC_FOLDER="src"
SHADERS_DIR="shaders"
SPV_DIR="compiledshaders"

# Create build folder if it doesn't exist
mkdir -p "$BUILD_FOLDER"
mkdir -p "$SPV_DIR"

# Compile shaders with glslc (if available)
if command -v glslc >/dev/null 2>&1; then
    echo "Compiling shaders → $SPV_DIR"
    # Find common shader stages and compile them
    while IFS= read -r -d '' src; do
        base=$(basename "$src")
        name="${base%.*}"
        ext="${base##*.}"
        out="$SPV_DIR/$name.$ext.spv"
        echo "  glslc $src -> $out"
        glslc "$src" -o "$out"
    done < <(find "$SHADERS_DIR" -type f \( -name "*.vert" -o -name "*.frag" -o -name "*.comp" -o -name "*.geom" -o -name "*.tesc" -o -name "*.tese" \) -print0)
else
    echo "Warning: glslc not found. Skipping shader compilation. Install Vulkan SDK or glslc to enable."
fi

# List C and C++ source files separately
c_src_files=(
    "$SRC_FOLDER/main.c"
    "$SRC_FOLDER/ext.c"
    "$SRC_FOLDER/initialise.c"
    "$SRC_FOLDER/helpers.c"
    "$SRC_FOLDER/descriptor.c"
    "$SRC_FOLDER/material.c"

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
