# Insight Viewer Windows

This directory contains the Windows executable for the Insight viewer application and its default configuration file. The program is used for debugging UVC video streams, IMU/VIO data, and HID sensor data.

## Directory Structure

- `Insight-Viewer.exe`: Windows executable
- `configs/default.json`: Default runtime configuration

## Running the Application

1. Double-click `Insight-Viewer.exe`
2. Or run from the command line:

```powershell
.\Insight-Viewer.exe
```

3. Specify a custom configuration file:

```powershell
.\Insight-Viewer.exe .\configs\default.json
```

If no configuration file is specified, the program will load `configs/default.json` from the current directory by default.

## Configuration File

`configs/default.json` contains the following main configuration sections:

- `app`: Application name, ImGui docking/viewport, and V-sync settings
- `video.streams`: UVC camera stream configuration, including vid/pid/interface, resolution, frame rate, and format
- `sensor`: HID sensor device information and sampling frequencies

The example configuration is preconfigured for standard Insight devices. If using a different device, adjust the `vid/pid/interface` values accordingly.

## Important Notes

- Graphics driver and OpenGL support are required to run the application.
- Ensure that the system can access the target UVC camera and HID devices.
- If the program fails to open devices, verify that the `vid/pid/interface` values in `configs/default.json` match your actual hardware.

## Disclaimer

This project is currently a viewer skeleton, primarily for debugging and preview purposes. It is not yet a production-ready application. The Windows version uses the same configuration structure and is suitable for debugging Insight UVC/HID devices in Windows environments.
