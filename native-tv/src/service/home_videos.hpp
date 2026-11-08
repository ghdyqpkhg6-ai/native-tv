#pragma once
// Fork: Home's live wallpapers: video files in <dataRoot>/home-videos, or in a "NativeTV" folder at
// the top of a USB drive plugged into the TV, chosen per profile in the
// Home layout ("liveWallpaper") and played under Home by the TV's own media pipeline
// (daemon/home_video_player.hpp), so the decoding is the TV's hardware decoder's.
//
// A file may come with a "<name>.credit" text file whose first line is shown on Home while the
// video plays: artwork shared on condition of attribution says whose it is.
#include "core/json.hpp"
#include <string>

namespace nativetv {
namespace service {

// A file name Home may play: letters, digits, '.', '_', '-' and spaces, no leading dot, a video
// extension (.mp4, .m4v, .mov, .mkv, .webm, .ts), at most 120 bytes.
bool homeVideoName(const std::string &name);
// A video's id: a file name of home-videos, or "usb/<device>/<file name>" for one on a USB drive
// ("usb/sda1/aurora.mp4"), the device as the TV names its partition.
bool homeVideoId(const std::string &id);
// [{"id": "black-hole.mp4", "title": "black hole", "credit": "..."}]: the TV's own, sorted by name,
// then each USB drive's ("aurora (USB)"). A drive's videos are listed while it is plugged in.
Json homeVideoList(const std::string &dataRoot);
// The file's full path when `id` is one of the listed videos, else empty (a drive unplugged).
std::string homeVideoPath(const std::string &dataRoot, const std::string &id);

} // namespace service
} // namespace nativetv
