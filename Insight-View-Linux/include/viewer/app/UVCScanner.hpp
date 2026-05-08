#pragma once
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <sys/stat.h>
#include <string>
#include <vector>

namespace viewer {

struct UVCDeviceInfo {
    std::string video_dev;   // /dev/videoX
    std::string usb_path;
    uint16_t vid;
    uint16_t pid;
    std::string serial;
    std::string product;
    std::string manufacturer;
    int index;               // 从video节点提取的数字
};

// 扫描所有UVC设备，返回匹配指定VID/PID的设备列表（按video索引排序）
std::vector<UVCDeviceInfo> scan_uvc_devices(uint16_t target_vid, uint16_t target_pid);
std::vector<std::string> scan_hid_devices(uint16_t target_vid, uint16_t target_pid);

} // namespace viewer