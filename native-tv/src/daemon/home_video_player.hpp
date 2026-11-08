#pragma once
// Fork: plays Home's live wallpaper (service/home_videos.hpp) through LG's media server
// (com.webos.media), for the TV app's id, so the TV's hardware decoder shows it on the video plane
// under the app's window; Home leaves its background see-through where it should show.
//
// Home asks for the video every few seconds while it is on screen. The player loads and plays the
// file, restarts it at its end (the subscription's endOfStream), and unloads it when Home asks for
// nothing, asks for another file, or stops asking for 20 s (the TV app closed or crashed), so the
// decoder is never held for a screen that is gone.
#include "core/json.hpp"
#include "daemon/logger.hpp"
#include "daemon/luna.hpp"
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace daemon {

class HomeVideoPlayer {
  public:
    HomeVideoPlayer(LunaClient &luna, Logger *logger);
    ~HomeVideoPlayer();
    HomeVideoPlayer(const HomeVideoPlayer &) = delete;
    HomeVideoPlayer &operator=(const HomeVideoPlayer &) = delete;

    // `path`: the file to keep playing, or empty to stop. Answers {playing, loading, error}.
    Json want(const std::string &path);

  private:
    struct State {
        std::mutex mutex;
        std::string path, mediaId, error;
        unsigned generation = 0;
        bool loading = false;
        std::chrono::steady_clock::time_point lastWant;
        std::shared_ptr<LunaSubscription> events;
    };
    static void start(LunaClient &luna, Logger *logger, const std::shared_ptr<State> &state,
                      std::unique_lock<std::mutex> &lock);
    static void stop(LunaClient &luna, const std::shared_ptr<State> &state, std::unique_lock<std::mutex> &lock);
    void watch();

    LunaClient &luna_;
    Logger *logger_;
    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::mutex watchMutex_;
    std::condition_variable watchWake_;
    bool stopping_ = false;
    std::thread watcher_;
};

} // namespace daemon
} // namespace nativetv
