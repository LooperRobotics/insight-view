#!/bin/bash
# InsightViewer Startup Script

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Set the library path
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# Set the font path
export FREETYPE_PROPERTIES="truetype:interpreter-version=35"

# Check the necessary system libraries
check_lib() {
    if ! ldconfig -p 2>/dev/null | grep -q "$1"; then
        echo "Warning: $1 not found"
        return 1
    fi
    return 0
}

# Check the key dependencies
MISSING_LIBS=""
for lib in libGL.so libGLU.so libSDL2-2.0.so.0; do
    if ! check_lib "$lib"; then
        MISSING_LIBS="$MISSING_LIBS $lib"
    fi
done

if [ -n "$MISSING_LIBS" ]; then
    echo "========================================="
    echo "Warning: The following system libraries are missing:"
    echo "$MISSING_LIBS"
    echo ""
    echo "Please run the following command to install dependencies:"
    echo "  sudo apt update"
    echo "  sudo apt install libgl1-mesa-glx libglu1-mesa libsdl2-2.0-0"
    echo "========================================="
fi

# Run the program
exec "$SCRIPT_DIR/bin/InsightViewer" "$@"
