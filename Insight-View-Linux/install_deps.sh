#!/bin/bash
set -e
REQUIRED_PACKAGES=(
    libsdl2-2.0-0
    libturbojpeg
    libhidapi-hidraw0
    libgl1-mesa-glx
    libglu1-mesa
)
MISSING=()
for pkg in "${REQUIRED_PACKAGES[@]}"; do
    if ! dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "install ok installed"; then
        MISSING+=("$pkg")
    else
        echo "Package $pkg already installed."
    fi
done
if [ ${#MISSING[@]} -ne 0 ]; then
    echo "Installing missing packages: ${MISSING[*]}"
    sudo apt update
    sudo apt install -y "${MISSING[@]}"
else
    echo "All required packages are already installed."
fi
