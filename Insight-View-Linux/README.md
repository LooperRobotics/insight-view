# Insight Viewer Linux

This directory contains the Linux executable for the Insight Viewer application, along with its default configuration file and helper scripts for managing runtime dependencies and launching the program. The application is designed for debugging UVC video streams, IMU/VIO data, and HID sensor data.

## Table of Contents

- [Directory Structure](#directory-structure)
- [Prerequisites](#prerequisites)
- [Running the Application](#running-the-application)
- [Configuration File](#configuration-file)
- [Important Notes](#important-notes)
- [Disclaimer](#disclaimer)

## Directory Structure

- `bin/Insight_Viewer`: Linux executable binary.
- `lib/`: Directory containing custom and third-party shared libraries (e.g., `libinsight9.so`, `libturbojpeg.so`, `libhidapi-hidraw.so`).
- `configs/default.json`: Default runtime configuration file.
- `run.sh`: Launch script that sets the library path and executes the binary.
- `install_deps.sh`: Optional script to install missing system packages (e.g., OpenGL, SDL2).

## Prerequisites

- A Linux system with a working OpenGL implementation.
- Access to UVC cameras and HID devices (may require user permissions).

## Running the Application

1. **Make the scripts executable (first time only):**
   ```bash
   chmod +x run.sh install_deps.sh
   ```

2. **Install required system dependencies (optional but recommended):**
   ```bash
   ./install_deps.sh
   ```
   This script attempts to install `libgl1-mesa-glx`, `libglu1-mesa`, and `libsdl2-2.0-0` via `apt`. If you are not on a Debian/Ubuntu-based system, install the equivalent packages manually.

3. **Launch the viewer:**
   ```bash
   sudo ./run.sh
   ```
   The script automatically sets `LD_LIBRARY_PATH` to include the bundled `lib/` directory, eliminating the need for additional environment setup.

4. **Specify a custom configuration file (optional):**
   ```bash
   ./run.sh ./configs/another_config.json
   ```

5. **Run the executable directly (advanced):**
   If you prefer to run the executable directly, manually set the library path:
   ```bash
   export LD_LIBRARY_PATH=./lib:$LD_LIBRARY_PATH
   sudo ./bin/Insight_Viewer
   ```

## Configuration File

The `configs/default.json` file shares the same structure as the Windows version and includes the following sections:

- **app**: Application settings, including name, ImGui docking/viewport configuration, and V-sync options.
- **video.streams**: UVC camera stream configurations, such as VID/PID/interface, resolution, frame rate, and format.
- **sensor**: HID sensor device details and sampling frequencies.

The provided example is preconfigured for standard Insight devices. Modify the VID/PID/interface values if your hardware setup differs.

## Important Notes

### Graphics and OpenGL
The application requires a functional OpenGL implementation. Most Linux systems include this by default. If issues arise, install your graphics drivers and the necessary OpenGL packages (`libgl1-mesa-glx`, `libglu1-mesa`).

### Device Access
To access UVC cameras (`/dev/video*`) and HID devices (`/dev/hidraw*`), your user account may need appropriate permissions. Running with `sudo`.

### Missing Libraries
The bundled `lib/` directory includes `libturbojpeg.so` and `libhidapi-hidraw.so` to prevent package lookup issues. The `install_deps.sh` script installs only core system libraries (OpenGL, SDL2). If you encounter missing library errors, ensure your distribution has the required OpenGL packages installed.

## Disclaimer

This project is currently a viewer skeleton, intended primarily for debugging and preview purposes. It is not yet a production-ready application. The Linux version mirrors the configuration structure of the Windows version and is suitable for debugging Insight UVC/HID devices in Linux environments.