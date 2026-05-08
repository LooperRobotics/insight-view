# uvc-viewer

Linux-first mock-first UVC/HID debugging viewer skeleton.

## Quick start

```bash
cd /home/ubuntu/.openclaw/workspace/uvc-viewer
chmod +x scripts/bootstrap_linux.sh
./scripts/bootstrap_linux.sh
sudo ./build/Insight_Viewer
```

If bootstrap succeeds, build artifacts should appear under `build/`.

## Manual build

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  pkg-config \
  libsdl2-dev \
  libgl1-mesa-dev \
  libfmt-dev \
  libspdlog-dev \
  nlohmann-json3-dev \
  libturbojpeg0-dev

cmake -S . -B build
cmake --build build -j"$(nproc)"
```

## Compile failure checklist

### 1) OpenGL not found
Symptoms:
- `Could NOT find OpenGL`

Fix:
```bash
sudo apt-get install -y libgl1-mesa-dev
```

### 2) SDL2 not found
Symptoms:
- `Could NOT find SDL2`
- `sdl2.pc not found`

Fix:
```bash
sudo apt-get install -y libsdl2-dev pkg-config
pkg-config --modversion sdl2
```

### 3) fmt / spdlog / nlohmann_json not found
Fix:
```bash
sudo apt-get install -y libfmt-dev libspdlog-dev nlohmann-json3-dev
```

### 4) turbojpeg not found
Current skeleton does not strictly require runtime decode yet, but install it now for MJPEG work:
```bash
sudo apt-get install -y libturbojpeg0-dev
```

### 5) CMake configured before dependencies were installed
Clear and retry:
```bash
rm -rf build
cmake -S . -B build
cmake --build build -j"$(nproc)"
```

### 6) Wayland / X11 display issues on remote machine
If GUI app later builds but cannot launch, first check whether the machine has a graphical session and OpenGL context available.

Useful checks:
```bash
echo $DISPLAY
glxinfo | head
```

### 7) Verify dependency visibility
```bash
pkg-config --modversion sdl2 || true
pkg-config --modversion fmt || true
pkg-config --modversion spdlog || true
cmake --version
g++ --version
```

## Notes
- This repo is currently a skeleton, not a finished viewer.

