#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

log() { echo "[bootstrap] $*"; }

if ! command -v sudo >/dev/null 2>&1; then
  echo "sudo not found; please install dependencies manually." >&2
  exit 1
fi

# 处理 build 目录：如果存在则删除，然后重新创建
if [ -d "${BUILD_DIR}" ]; then
    log "Removing existing build directory..."
    rm -rf "${BUILD_DIR}"
fi
mkdir -p "${BUILD_DIR}"
log "Created fresh build directory."

# 定义所有需要安装的软件包
PACKAGES=(
  build-essential
  cmake
  pkg-config
  libsdl2-dev
  libgl1-mesa-dev
  libfmt-dev
  libspdlog-dev
  nlohmann-json3-dev
  libturbojpeg0-dev
  libhidapi-dev
)

# 检查哪些包尚未安装
MISSING_PACKAGES=()
for pkg in "${PACKAGES[@]}"; do
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
        MISSING_PACKAGES+=("$pkg")
    else
        log "Package $pkg already installed."
    fi
done

# 如果有缺失的包，则更新索引并安装
if [ ${#MISSING_PACKAGES[@]} -ne 0 ]; then
    log "Updating apt package index..."
    sudo apt-get update
    log "Installing missing packages: ${MISSING_PACKAGES[*]}"
    sudo apt-get install -y "${MISSING_PACKAGES[@]}"
else
    log "All required packages are already installed."
fi


log "Configuring project..."
cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"

log "Building project..."
cmake --build "${BUILD_DIR}" -j"$(nproc)"

log "Done. Binary location (expected): ${BUILD_DIR}/Insight_Viewer"
