#pragma once
// Fork: a Google account linked to the TV with OAuth 2.0 for TVs and limited-input devices, with
// read-only YouTube access, for one Home row: the latest uploads of the channels it subscribes to.
//
// The OAuth client is the owner's own, made in Google Cloud ("TVs and Limited Input devices") and
// set in config.json as youtubeClientId and youtubeClientSecret. Signing in shows a code on the TV
// that is entered at google.com/device on a phone; the TV never sees the password. Only the refresh
// token is kept, in youtube-account.json (mode 0600) beside the server's other data, with the last
// row so Home has it after a restart. Signing out revokes the token with Google and deletes the file.
//
// YouTube's API gives no recommendations, watch history or playback progress, so none are asked
// for and none are made up: the row is subscriptions only. A refresh costs about 33 units of the
// project's 10,000 a day and runs at most every two hours, only while Home asks for the feed.
#include "core/json.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace nativetv {
namespace service {

class YouTubeAccount {
  public:
    // POSTs `form` (application/x-www-form-urlencoded) when it is not empty, else GETs; `bearer` is
    // an access token for the Authorization header. Tests pass their own.
    using Http = std::function<bool(const std::string &url, const std::string &form,
                                    const std::string &bearer, Json &reply, long &status,
                                    std::string &error)>;
    explicit YouTubeAccount(std::string dataRoot, Http http = Http());
    ~YouTubeAccount();
    YouTubeAccount(const YouTubeAccount &) = delete;
    YouTubeAccount &operator=(const YouTubeAccount &) = delete;

    // {configured, linked, pending: {code, url, expiresInMs}, error}
    Json status(const std::string &clientId, const std::string &clientSecret);
    // Starts signing in: asks Google for a code to show. The worker then waits for the person.
    Json link(const std::string &clientId, const std::string &clientSecret);
    Json unlink();
    // The "subs" row for Home's feed, or invalid Json while there is none; asks for a fresh one when
    // the last is two hours old.
    Json subscriptionsRow(const std::string &clientId, const std::string &clientSecret);

  private:
    void workerLoop();
    void pollSignIn(const std::string &clientId, const std::string &clientSecret,
                    const std::string &deviceCode);
    void refreshRow(const std::string &clientId, const std::string &clientSecret);
    bool accessToken(const std::string &clientId, const std::string &clientSecret, std::string &token);
    bool request(const std::string &url, const std::string &form, const std::string &bearer, Json &reply,
                 long &status, std::string &error);
    void loadLocked();
    void saveLocked();

    std::string path_;
    Http http_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::thread worker_;
    std::atomic<bool> stopping_{false};
    bool loaded_ = false;
    // Signing in.
    std::string deviceCode_, userCode_, verificationUrl_, signInClientId_, signInClientSecret_;
    int64_t signInEndsMs_ = 0, nextPollMs_ = 0, pollGapMs_ = 5000;
    // Linked.
    std::string refreshToken_, accessToken_, error_;
    int64_t accessEndsMs_ = 0;
    // The row.
    Json row_;
    int64_t rowAtMs_ = 0, rowTriedMs_ = 0;
    bool rowWanted_ = false, rowBusy_ = false;
    std::string rowClientId_, rowClientSecret_;
};

} // namespace service
} // namespace nativetv
