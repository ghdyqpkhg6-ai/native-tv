#include "daemon/home_video_player.hpp"
#include <utility>

namespace nativetv {
namespace daemon {
namespace {
// The pipeline belongs to the TV app, whose window the video plays under.
const char *const kAppId = "com.tvweb.dashboard";
const auto kIdle = std::chrono::seconds(20);
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

Json HomeVideoPlayer::want(const std::string &path) {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->lastWant = std::chrono::steady_clock::now();
    if (path != state_->path) {
        stop(luna_, state_, lock);
        state_->path = path;
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
    Json inner = Json::object();
    inner.set("option", option);
    Json payload = Json::object();
    payload.set("uri", Json::text("file://" + state->path));
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
            // Full screen on the video plane, as a TV app's own video is.
            client->call("com.webos.media/switchToFullscreen", withMedia(mediaId), ignore, kAppId);
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
}

void HomeVideoPlayer::watch() {
    std::unique_lock<std::mutex> wait(watchMutex_);
    while (!stopping_) {
        watchWake_.wait_for(wait, std::chrono::seconds(2));
        if (stopping_)
            break;
        std::unique_lock<std::mutex> lock(state_->mutex);
        if (!state_->path.empty() && std::chrono::steady_clock::now() - state_->lastWant > kIdle) {
            if (logger_)
                logger_->info("home video: Home stopped asking; unloading");
            stop(luna_, state_, lock);
        }
    }
}

} // namespace daemon
} // namespace nativetv
