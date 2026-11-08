#include "service/home_videos.hpp"
#include <algorithm>
#include <cctype>
#include <dirent.h>
#include <fstream>
#include <sys/stat.h>
#include <vector>

namespace nativetv {
namespace service {
namespace {
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
} // namespace

bool homeVideoName(const std::string &name) {
    if (name.empty() || name.size() > 120 || name[0] == '.' || extensionOf(name).empty())
        return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == ' ';
    });
}

Json homeVideoList(const std::string &dataRoot) {
    const std::string directory = videoDirectory(dataRoot);
    std::vector<std::string> names;
    if (DIR *dir = opendir(directory.c_str())) {
        while (const dirent *entry = readdir(dir)) {
            const std::string name = entry->d_name;
            if (!homeVideoName(name))
                continue;
            struct stat info;
            if (stat((directory + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0)
                names.push_back(name);
            if (names.size() >= 64)
                break;
        }
        closedir(dir);
    }
    std::sort(names.begin(), names.end());
    Json list = Json::array();
    for (const std::string &name : names) {
        Json item = Json::object();
        item.set("id", Json::text(name));
        item.set("title", Json::text(titleOf(name)));
        item.set("credit", Json::text(creditOf(directory + "/" + name)));
        list.add(item);
    }
    return list;
}

std::string homeVideoPath(const std::string &dataRoot, const std::string &name) {
    if (!homeVideoName(name))
        return "";
    const std::string path = videoDirectory(dataRoot) + "/" + name;
    struct stat info;
    if (stat(path.c_str(), &info) != 0 || !S_ISREG(info.st_mode))
        return "";
    return path;
}

} // namespace service
} // namespace nativetv
