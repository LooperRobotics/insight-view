#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"
if ! ldconfig -p | grep -q libGLU.so; then
    echo "Warning: libGLU.so not found, please install libglu1-mesa (e.g. sudo apt install libglu1-mesa)"
fi
exec "$SCRIPT_DIR/bin/Insight_Viewer" "$@"
