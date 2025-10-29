#!/bin/bash
# Format all C and H files in the project
echo "Formatting all C and H files..."
find . -name "*.c" -o -name "*.h" | xargs clang-format -i
echo "Done!"