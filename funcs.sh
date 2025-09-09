
#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <src-folder>"
    exit 1
fi

SRC_DIR="$1"

# Find C/C++ source files and loop over them
find "$SRC_DIR" -type f \( -name "*.c" -o -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) | while read -r FILE; do
    echo "### $FILE"

    # Remove comments and preprocessors, then join lines into one
    cleaned=$(sed 's/\/\/.*//;s/\/\*.*\*\///g' "$FILE" | tr '\n' ' ')

    # Extract function definitions (skip prototypes ending with ;)
    echo "$cleaned" | grep -oP '\b\w[\w\s\*]*\b\s+\**\b\w+\s*\([^;{]*\)\s*\{' | while read -r line; do
        # Clean up spacing
        func=$(echo "$line" | sed 's/\s\+/ /g' | sed 's/{.*//')
        echo "    $func"
    done

    echo
done

