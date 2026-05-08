#include "viewer/app/UVCScanner.hpp"
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include <linux/videodev2.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>

namespace viewer {

static bool is_uvc_device(const std::string& video_dev) {
    int fd = open(video_dev.c_str(), O_RDWR);
    if (fd < 0) return false;
    struct v4l2_capability cap;
    bool is_uvc = false;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
        if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
            is_uvc = true;
        }
    }
    close(fd);
    return is_uvc;
}

static std::string read_sysfs_file(const std::string& path) {
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return "";
    char buffer[256];
    if (fgets(buffer, sizeof(buffer), fp)) {
        // 去掉末尾换行
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len-1] == '\n') buffer[len-1] = '\0';
        fclose(fp);
        return buffer;
    }
    fclose(fp);
    return "";
}

static std::string find_usb_path_from_video(const std::string& video_sysfs) {
    char target[512];
    // 读取 device 链接的真实路径
    std::string device_link = video_sysfs + "/device";
    if (realpath(device_link.c_str(), target) == nullptr) return "";

    std::string usb_path = target;
    // 向上查找直到找到包含 idVendor 的目录
    while (!usb_path.empty() && usb_path != "/") {
        std::string vid_path = usb_path + "/idVendor";
        if (access(vid_path.c_str(), F_OK) == 0) {
            return usb_path;
        }
        // 去掉最后一层目录
        size_t pos = usb_path.find_last_of('/');
        if (pos == std::string::npos) break;
        usb_path = usb_path.substr(0, pos);
    }
    return "";
}

static int extract_video_index(const std::string& video_dev) {
    // 从 "/dev/videoX" 提取数字 X
    size_t pos = video_dev.find_last_not_of("0123456789");
    if (pos == std::string::npos) return -1;
    return std::stoi(video_dev.substr(pos + 1));
}

std::vector<UVCDeviceInfo> scan_uvc_devices(uint16_t target_vid, uint16_t target_pid) {
    std::vector<UVCDeviceInfo> result;
    DIR* dir = opendir("/sys/class/video4linux");
    if (!dir) return result;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        std::string video_name = entry->d_name;
        std::string video_dev = "/dev/" + video_name;
        if (!is_uvc_device(video_dev)) continue;

        std::string video_sysfs = std::string("/sys/class/video4linux/") + video_name;
        std::string usb_path = find_usb_path_from_video(video_sysfs);
        if (usb_path.empty()) continue;

        UVCDeviceInfo info;
        info.video_dev = video_dev;
        info.usb_path = usb_path;
        info.index = extract_video_index(video_dev);

        // 读取 VID/PID
        std::string vid_str = read_sysfs_file(usb_path + "/idVendor");
        std::string pid_str = read_sysfs_file(usb_path + "/idProduct");
        if (!vid_str.empty()) info.vid = std::stoul(vid_str, nullptr, 16);
        if (!pid_str.empty()) info.pid = std::stoul(pid_str, nullptr, 16);

        // 读取其他信息
        info.serial = read_sysfs_file(usb_path + "/serial");
        info.product = read_sysfs_file(usb_path + "/product");
        info.manufacturer = read_sysfs_file(usb_path + "/manufacturer");

        // 只保留匹配 VID/PID 的设备
        if (info.vid == target_vid && info.pid == target_pid) {
            result.push_back(info);
        }
    }
    closedir(dir);

    // 按 video 索引排序
    std::sort(result.begin(), result.end(),
              [](const UVCDeviceInfo& a, const UVCDeviceInfo& b) {
                  return a.index < b.index;
              });
    return result;
}

static std::string find_usb_path_from_hidraw(const std::string& hidraw_sysfs) {
    char target[512];
    // hidraw_sysfs 形如 "/sys/class/hidraw/hidraw0"
    std::string device_link = hidraw_sysfs + "/device";
    if (realpath(device_link.c_str(), target) == nullptr) return "";

    std::string path = target;
    // 向上查找直到找到包含 idVendor 的目录
    while (!path.empty() && path != "/") {
        std::string vid_path = path + "/idVendor";
        if (access(vid_path.c_str(), F_OK) == 0) {
            return path;
        }
        size_t pos = path.find_last_of('/');
        if (pos == std::string::npos) break;
        path = path.substr(0, pos);
    }
    return "";
}

static int extract_hidraw_index(const std::string& hidraw_name) {
    // hidraw_name 形如 "hidraw0"
    size_t pos = hidraw_name.find_first_of("0123456789");
    if (pos == std::string::npos) return -1;
    return std::stoi(hidraw_name.substr(pos));
}

std::vector<std::string> scan_hid_devices(uint16_t target_vid, uint16_t target_pid) {
    std::vector<std::pair<int, std::string>> devices; // (index, path)
    DIR* dir = opendir("/sys/class/hidraw");
    if (!dir) return {};

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        std::string hidraw_name = entry->d_name;
        std::string hidraw_sysfs = std::string("/sys/class/hidraw/") + hidraw_name;
        std::string usb_path = find_usb_path_from_hidraw(hidraw_sysfs);
        if (usb_path.empty()) continue;

        // 读取 VID/PID
        std::string vid_str, pid_str;
        std::ifstream vid_file(usb_path + "/idVendor");
        std::ifstream pid_file(usb_path + "/idProduct");
        if (vid_file.is_open()) std::getline(vid_file, vid_str);
        if (pid_file.is_open()) std::getline(pid_file, pid_str);
        if (vid_str.empty() || pid_str.empty()) continue;

        uint16_t vid = std::stoul(vid_str, nullptr, 16);
        uint16_t pid = std::stoul(pid_str, nullptr, 16);
        if (vid == target_vid && pid == target_pid) {
            int index = extract_hidraw_index(hidraw_name);
            if (index >= 0) {
                std::string dev_path = "/dev/" + hidraw_name;
                devices.emplace_back(index, dev_path);
            }
        }
    }
    closedir(dir);

    // 按index排序
    std::sort(devices.begin(), devices.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::string> result;
    for (const auto& p : devices) result.push_back(p.second);
    return result;
}

} // namespace viewer