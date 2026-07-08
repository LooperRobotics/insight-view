#!/bin/bash
# InsightViewer 启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 设置库路径
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# 设置字体路径 (如果需要)
export FREETYPE_PROPERTIES="truetype:interpreter-version=35"

# 检查必要的系统库
check_lib() {
    if ! ldconfig -p 2>/dev/null | grep -q "$1"; then
        echo "Warning: $1 not found"
        return 1
    fi
    return 0
}

# 检查关键依赖
MISSING_LIBS=""
for lib in libGL.so libGLU.so libSDL2-2.0.so.0; do
    if ! check_lib "$lib"; then
        MISSING_LIBS="$MISSING_LIBS $lib"
    fi
done

if [ -n "$MISSING_LIBS" ]; then
    echo "========================================="
    echo "警告: 缺少以下系统库:"
    echo "$MISSING_LIBS"
    echo ""
    echo "请运行以下命令安装依赖:"
    echo "  sudo apt update"
    echo "  sudo apt install libgl1-mesa-glx libglu1-mesa libsdl2-2.0-0"
    echo "========================================="
fi

# 运行程序
exec "$SCRIPT_DIR/bin/InsightViewer" "$@"
