#!/bin/bash
set -e

REQUIRED_PACKAGES=(
    libsdl2-2.0-0
    libturbojpeg0
    libhidapi-hidraw0
    libgl1-mesa-glx
    libglu1-mesa
    libv4l-0
    libuvc0
    nlohmann-json3-dev
)

echo "========================================="
echo "InsightViewer 依赖安装脚本"
echo "========================================="

MISSING=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
        echo "✓ $pkg 已安装"
    else
        echo "✗ $pkg 未安装"
        MISSING+=("$pkg")
    fi
done

if [ ${#MISSING[@]} -ne 0 ]; then
    echo ""
    echo "安装缺失的包: ${MISSING[*]}"
    sudo apt update
    sudo apt install -y "${MISSING[@]}"
    echo ""
    echo "所有依赖安装完成!"
else
    echo ""
    echo "所有依赖已安装!"
fi

echo ""
echo "运行 ./run.sh 启动程序"
