#!/bin/bash

if [ $# -ne 1 ]; then
    echo "Usage: $0 <file.c>"
    exit 1
fi

FILE="$1"

# Remove comments and preprocessors, then extract functions
# Handles functions declared like:
# int add(int a, int b) {
# void foo(void)
# static inline int bar(double x, float y)

# Remove comments and join lines
cleaned=$(sed 's/\/\/.*//;s/\/\*.*\*\///g' "$FILE" | tr '\n' ' ')

# Extract using regex with grep
echo "$cleaned" | grep -oP '\b\w[\w\s\*]*\b\s+\**\b\w+\s*\([^;{]*\)\s*\{' | while read -r line; do
    # Clean up the line
    func=$(echo "$line" | sed 's/\s\+/ /g' | sed 's/{.*//')
    echo "$func"
done

