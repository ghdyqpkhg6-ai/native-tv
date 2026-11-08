#include "daemon/home_video_player.hpp"
#include <utility>

namespace nativetv {
namespace daemon {
namespace {
// The pipeline belongs to the TV app, whose window the video plays under.
const char *const kAppId = "com.tvweb.dashboard";
// Home asks every 5 s while the TV app is in front. Quiet longer than this, the player asks the TV
// what is in front instead: one of LG's own system screens over the app (Settings, the input list,
// volume, voice, notifications) keeps the video; another app (YouTube, Netflix, Live TV, an input)
// unloads it, so that app has the decoders. Without an answer for a minute, it unloads anyway.
const auto kQuiet = std::chrono::seconds(8);
const auto kUnanswered = std::chrono::seconds(60);

bool systemScreen(const std::string &appId) {
    if (appId.empty() || appId == kAppId || appId == "com.webos.app.home")
        return true;
    for (const char *part : {"settings", "inputcommon", "voice", "notification", "volume", "surfacemanager",
                             "quickset", "menu", "accessibility", "dialog", "popup"})
        if (appId.find(part) != std::string::npos)
            return true;
    return false;
}
const auto kCallTimeout = std::chrono::milliseconds(8000);

Json withMedia(const std::string &mediaId) {
    Json payload = Json::object();
    payload.set("mediaId", Json::text(mediaId));
    return payload;
}
void ignore(const LunaReply &) {}
} // namespace

HomeVideoPlayer::HomeVideoPlayer(LunaClient &luna, Logger *logger) : luna_(luna), logger_(logger) {
    watcher_ = std::thread(&HomeVideoPlayer::watch, this);
}

HomeVideoPlayer::~HomeVideoPlayer() {
    {
        std::lock_guard<std::mutex> lock(watchMutex_);
        stopping_ = true;
    }
    watchWake_.notify_all();
    if (watcher_.joinable())
        watcher_.join();
    std::unique_lock<std::mutex> lock(state_->mutex);
    stop(luna_, state_, lock);
}

Json HomeVideoPlayer::want(const std::string &path, const std::string &windowId, bool sound) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->lastWant = std::chrono::steady_clock::now();
    if (sound != state_->sound) {
        state_->sound = sound;
        // The playing pipeline follows at once; a new one starts at this volume.
        if (!state_->mediaId.empty()) {
            Json volume = withMedia(state_->mediaId);
            volume.set("volume", Json::numeric(sound ? 100 : 0));
            luna_.call("com.webos.media/setVolume", volume, ignore, kAppId);
        }
    }
    // A new window (the TV app started again) needs a pipeline loaded for it.
    if (path != state_->path || (!path.empty() && windowId != state_->windowId)) {
        stop(luna_, state_, lock);
        state_->path = path;
        state_->windowId = windowId;
        state_->error.clear();
        if (!path.empty())
            start(luna_, logger_, state_, lock);
    }
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("playing", Json::flag(!state_->mediaId.empty()));
    result.set("loading", Json::flag(state_->loading));
    result.set("error", Json::text(state_->error));
    return result;
}

void HomeVideoPlayer::start(LunaClient &luna, Logger *logger, const std::shared_ptr<State> &state,
                            std::unique_lock<std::mutex> &) {
    const unsigned generation = ++state->generation;
    state->loading = true;
    Json option = Json::object();
    option.set("appId", Json::text(kAppId));
    if (!state->windowId.empty())
        option.set("windowId", Json::text(state->windowId));
    Json inner = Json::object();
    inner.set("option", option);
    Json payload = Json::object();
    std::string uri = "file://";
    for (char c : state->path) // a USB drive's folder or file may have spaces in its name
        uri += c == ' ' ? std::string("%20") : std::string(1, c);
    payload.set("uri", Json::text(uri));
    payload.set("type", Json::text("media"));
    payload.set("payload", inner);
    const std::weak_ptr<State> weak = state;
    LunaClient *client = &luna;
    const bool queued = luna.call(
        "com.webos.media/load", payload,
        [weak, generation, client, logger](const LunaReply &reply) {
            const std::shared_ptr<State> state = weak.lock();
            if (!state)
                return;
            const std::string mediaId = reply.response["mediaId"].string();
            std::lock_guard<std::mutex> lock(state->mutex);
            if (generation != state->generation) {
                // Stopped or changed while loading: this pipeline is nobody's.
                if (!mediaId.empty())
                    client->call("com.webos.media/unload", withMedia(mediaId), ignore, kAppId);
                return;
            }
            state->loading = false;
            if (mediaId.empty() || !reply.response["returnValue"].boolean()) {
                state->error = reply.error.empty() ? reply.response["errorText"].string("The TV did not load the video")
                                                   : reply.error;
                if (logger)
                    logger->warning("home video: load failed: " + state->error);
                return;
            }
            state->mediaId = mediaId;
            client->call("com.webos.media/play", withMedia(mediaId), ignore, kAppId);
            // Silent unless Home settings switch the wallpaper's sound on.
            Json volume = withMedia(mediaId);
            volume.set("volume", Json::numeric(state->sound ? 100 : 0));
            client->call("com.webos.media/setVolume", volume, ignore, kAppId);
            // A loop: at its end the file starts again.
            LunaSubscriptionHandlers handlers;
            handlers.message = [weak, generation, client, mediaId](const Json &event) {
                if (!event.has("endOfStream"))
                    return;
                const std::shared_ptr<State> state = weak.lock();
                if (!state)
                    return;
                {
                    std::lock_guard<std::mutex> lock(state->mutex);
                    if (generation != state->generation || state->mediaId != mediaId)
                        return;
                }
                Json seek = withMedia(mediaId);
                seek.set("position", Json::numeric(0));
                client->call("com.webos.media/seek", seek, [client, mediaId](const LunaReply &) {
                    client->call("com.webos.media/play", withMedia(mediaId), ignore, kAppId);
                }, kAppId);
            };
            state->events = client->subscribe("com.webos.media/subscribe", withMedia(mediaId), kAppId,
                                              std::move(handlers));
            if (state->events)
                (void)state->events->start();
            if (logger)
                logger->info("home video: playing " + state->path);
        },
        kAppId, false, kCallTimeout);
    if (!queued) {
        state->loading = false;
        state->error = "Too many calls to the TV are waiting";
    }
}

void HomeVideoPlayer::stop(LunaClient &luna, const std::shared_ptr<State> &state, std::unique_lock<std::mutex> &) {
    ++state->generation;
    if (state->events) {
        state->events->stop();
        state->events.reset();
    }
    if (!state->mediaId.empty())
        luna.call("com.webos.media/unload", withMedia(state->mediaId), ignore, kAppId);
    state->mediaId.clear();
    state->loading = false;
    state->path.clear();
    state->windowId.clear();
}

void HomeVideoPlayer::watch() {
    std::unique_lock<std::mutex> wait(watchMutex_);
    while (!stopping_) {
        watchWake_.wait_for(wait, std::chrono::seconds(2));
        if (stopping_)
            break;
        std::unique_lock<std::mutex> lock(state_->mutex);
        const auto quiet = std::chrono::steady_clock::now() - state_->lastWant;
        if (!state_->path.empty() && quiet > kUnanswered) {
            if (logger_)
                logger_->info("home video: Home stopped asking; unloading");
            stop(luna_, state_, lock);
            continue;
        }
        if (!state_->path.empty() && quiet > kQuiet) {
            const std::weak_ptr<State> weak = state_;
            const std::string path = state_->path;
            LunaClient *client = &luna_;
            Logger *logger = logger_;
            luna_.call("com.webos.applicationManager/getForegroundAppInfo", Json::object(),
                       [weak, path, client, logger](const LunaReply &reply) {
                           const std::shared_ptr<State> state = weak.lock();
                           if (!state || reply.timedOut || !reply.error.empty() ||
                               !reply.response["returnValue"].boolean(false))
                               return; // no answer: kept until kUnanswered
                           const std::string front = reply.response["appId"].string();
                           std::unique_lock<std::mutex> lock(state->mutex);
                           if (state->path != path)
                               return;
                           if (systemScreen(front)) {
                               state->lastWant = std::chrono::steady_clock::now();
                               return;
                           }
                           if (logger)
                               logger->info("home video: " + front + " is in front; unloading");
                           stop(*client, state, lock);
                       });
            continue;
        }
        // Every 10 s: a pipeline the TV dropped (its resource policy can take the decoders back)
        // is loaded again, so Home never sits over a video that is gone.
        if (++checks_ % 5 != 0 || state_->mediaId.empty())
            continue;
        const std::weak_ptr<State> weak = state_;
        const std::string mediaId = state_->mediaId;
        LunaClient *client = &luna_;
        Logger *logger = logger_;
        luna_.call("com.webos.media/getPipelineState", withMedia(mediaId),
                   [weak, mediaId, client, logger](const LunaReply &reply) {
                       const std::shared_ptr<State> state = weak.lock();
                       if (!state || reply.timedOut || !reply.error.empty() ||
                           reply.response["returnValue"].boolean(true))
                           return;
                       std::unique_lock<std::mutex> lock(state->mutex);
                       if (state->mediaId != mediaId || state->path.empty())
                           return;
                       if (logger)
                           logger->info("home video: the pipeline is gone; loading it again");
                       const std::string path = state->path, windowId = state->windowId;
                       stop(*client, state, lock);
                       state->path = path;
                       state->windowId = windowId;
                       start(*client, logger, state, lock);
                   },
                   kAppId);
    }
}

} // namespace daemon
} // namespace nativetv
