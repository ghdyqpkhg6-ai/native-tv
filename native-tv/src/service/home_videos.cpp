#include "service/home_videos.hpp"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace nativetv {
namespace service {
namespace {
// The folder on a USB drive whose videos Home may play; the rest of the drive is never read.
const char *const kUsbFolder = "NativeTV";
const size_t kMaximumVideos = 64;

std::string videoDirectory(const std::string &dataRoot) {
    return (dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) + "/home-videos";
}
bool endsWith(const std::string &text, const std::string &suffix) {
    if (text.size() < suffix.size())
        return false;
    for (size_t i = 0; i < suffix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(text[text.size() - suffix.size() + i])) != suffix[i])
            return false;
    return true;
}
std::string extensionOf(const std::string &name) {
    for (const char *extension : {".mp4", ".m4v", ".mov", ".mkv", ".webm", ".ts"})
        if (endsWith(name, extension))
            return extension;
    return "";
}
// "black-hole_4k.mp4" -> "black hole 4k"
std::string titleOf(const std::string &name) {
    std::string title = name.substr(0, name.size() - extensionOf(name).size());
    std::replace(title.begin(), title.end(), '-', ' ');
    std::replace(title.begin(), title.end(), '_', ' ');
    return title;
}
std::string creditOf(const std::string &path) {
    std::ifstream file(path + ".credit");
    std::string line;
    if (!file || !std::getline(file, line))
        return "";
    while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
        line.pop_back();
    if (line.size() > 120)
        line.resize(120);
    // Printable text only: the line is drawn on the TV as it is.
    line.erase(std::remove_if(line.begin(), line.end(),
                              [](unsigned char c) { return c < 0x20 || c == 0x7f; }),
               line.end());
    return line;
}
bool regularFile(const std::string &path) {
    struct stat info;
    return stat(path.c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0;
}
// The playable files of one folder, sorted.
std::vector<std::string> videosIn(const std::string &directory) {
    std::vector<std::string> names;
    if (DIR *dir = opendir(directory.c_str())) {
        while (const dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (homeVideoName(name) && regularFile(directory + "/" + name))
                names.push_back(name);
            if (names.size() >= kMaximumVideos)
                break;
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    return names;
}
// "/proc/mounts" writes a space in a path as "\040".
std::string unescapeMount(const std::string &field) {
    std::string result;
    for (size_t i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && i + 3 < field.size() && std::isdigit(static_cast<unsigned char>(field[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(field[i + 2])) && std::isdigit(static_cast<unsigned char>(field[i + 3]))) {
            result.push_back(static_cast<char>((field[i + 1] - '0') * 64 + (field[i + 2] - '0') * 8 + (field[i + 3] - '0')));
            i += 3;
        } else {
            result.push_back(field[i]);
        }
    }
    return result;
}
bool deviceName(const std::string &name) {
    return !name.empty() && name.size() <= 16 &&
           std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isalnum(c); });
}
// USB drives as the TV has them mounted: block devices "/dev/sdXN" (USB storage; the TV's own
// flash is mmcblk), keyed by "sdXN".
std::vector<std::pair<std::string, std::string>> usbMounts() {
    std::vector<std::pair<std::string, std::string>> mounts;
    std::ifstream file("/proc/mounts");
    std::string line;
    while (std::getline(file, line)) {
        std::istringstream fields(line);
        std::string device, point;
        if (!(fields >> device >> point) || device.compare(0, 7, "/dev/sd") != 0)
            continue;
        const std::string name = device.substr(5);
        if (!deviceName(name))
            continue;
        bool known = false;
        for (const auto &mount : mounts)
            known = known || mount.first == name;
        if (!known)
            mounts.emplace_back(name, unescapeMount(point));
    }
    return mounts;
}
} // namespace

bool homeVideoName(const std::string &name) {
    if (name.empty() || name.size() > 120 || name[0] == '.' || extensionOf(name).empty())
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ';
    });
}

bool homeVideoId(const std::string &id) {
    if (homeVideoName(id))
        return true;
    // "usb/<device>/<file>"
    if (id.compare(0, 4, "usb/") != 0)
        return false;
    const size_t slash = id.find('/', 4);
    return slash != std::string::npos && deviceName(id.substr(4, slash - 4)) &&
           homeVideoName(id.substr(slash + 1));
}

Json homeVideoList(const std::string &dataRoot) {
    Json list = Json::array();
    const std::string directory = videoDirectory(dataRoot);
    for (const std::string &name : videosIn(directory)) {
        Json item = Json::object();
        item.set("id", Json::text(name));
        item.set("title", Json::text(titleOf(name)));
        item.set("credit", Json::text(creditOf(directory + "/" + name)));
        list.add(item);
    }
    for (const auto &mount : usbMounts()) {
        const std::string folder = mount.second + "/" + kUsbFolder;
        for (const std::string &name : videosIn(folder)) {
            if (list.size() >= kMaximumVideos * 2)
                break;
            Json item = Json::object();
            item.set("id", Json::text("usb/" + mount.first + "/" + name));
            item.set("title", Json::text(titleOf(name) + " (USB)"));
            item.set("credit", Json::text(creditOf(folder + "/" + name)));
            list.add(item);
        }
    }
    return list;
}

std::string homeVideoPath(const std::string &dataRoot, const std::string &id) {
    if (!homeVideoId(id))
        return "";
    std::string path;
    if (id.compare(0, 4, "usb/") == 0) {
        const size_t slash = id.find('/', 4);
        const std::string device = id.substr(4, slash - 4);
        for (const auto &mount : usbMounts())
            if (mount.first == device)
                path = mount.second + "/" + kUsbFolder + "/" + id.substr(slash + 1);
    } else {
        path = videoDirectory(dataRoot) + "/" + id;
    }
    return !path.empty() && regularFile(path) ? path : "";
}

} // namespace service
} // namespace nativetv
