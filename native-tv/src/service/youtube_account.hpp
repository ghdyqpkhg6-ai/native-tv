#pragma once
// Fork: Google accounts linked to the TV with OAuth 2.0 for TVs and limited-input devices, with
// read-only YouTube access, for one Home row: the latest uploads of the channels an account
// subscribes to.
//
// The OAuth client is the owner's own, made in Google Cloud ("TVs and Limited Input devices") and
// set in config.json as youtubeClientId and youtubeClientSecret. Signing in shows a code on the TV
// that is entered at google.com/device on a phone; the TV never sees the password.
//
// Several accounts can be linked, and each Home profile has its own: the profile's account is the
// one whose row Home shows. A profile that never chose one follows the first account linked. The
// row's first card (and the YouTube account page) switches the profile to the next account.
// Signing in from a profile adds the account (or renews it, for one already linked) and gives it
// to that profile. Signing out revokes that account's token with Google and forgets it on every
// profile.
//
// Only the refresh tokens are kept, in youtube-account.json (mode 0600) beside the server's other
// data, with each account's last row so Home has it after a restart. A file from before accounts
// (one "refreshToken") is read as a single account.
//
// YouTube's API gives no recommendations, watch history or playback progress, so none are asked
// for and none are made up: the row is subscriptions only. A refresh costs about 33 units of the
// project's 10,000 a day and runs at most every two hours per account, only for the account Home
// is showing.
#include "core/json.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

    // `profile` is Home's active profile ("main", "p2", ...) throughout.
    // {configured, linked, account: {id, name}, accounts: [{id, name, active}], pending: {code, url,
    //  expiresInMs}, error, videos}
    Json status(const std::string &profile, const std::string &clientId, const std::string &clientSecret);
    // Starts signing in an account for `profile`: asks Google for a code to show. The worker then
    // waits for the person.
    Json link(const std::string &profile, const std::string &clientId, const std::string &clientSecret);
    // Signs out the profile's account (or `accountId` when given): revoked with Google, forgotten on
    // every profile. A sign-in that is waiting is cancelled instead when there is no account.
    Json unlink(const std::string &profile, const std::string &accountId = "");
    // Gives `profile` the account `accountId`, or the one after its current one when `accountId` is
    // empty.
    Json use(const std::string &profile, const std::string &accountId);
    // The "subs" row of the profile's account for Home's feed, or invalid Json while there is none;
    // asks for a fresh one when the last is two hours old. The row carries the account's name and
    // the accounts to switch between, for the picker card.
    Json subscriptionsRow(const std::string &profile, const std::string &clientId,
                          const std::string &clientSecret);

  private:
    struct Account {
        std::string id, name, channelId, refreshToken, accessToken;
        int64_t accessEndsMs = 0;
        Json row;
        int64_t rowAtMs = 0, rowTriedMs = 0;
    };

    void workerLoop();
    void pollSignIn(const std::string &clientId, const std::string &clientSecret,
                    const std::string &deviceCode);
    void refreshRow(const std::string &accountId, const std::string &clientId,
                    const std::string &clientSecret);
    bool accessToken(const std::string &accountId, const std::string &clientId,
                     const std::string &clientSecret, std::string &token);
    // The account's channel name and id, asked once after it signs in.
    void identify(const std::string &accountId, const std::string &token);
    bool request(const std::string &url, const std::string &form, const std::string &bearer, Json &reply,
                 long &status, std::string &error);
    Account *findLocked(const std::string &id);
    // The profile's account: its own choice while that account is linked, else the first one.
    Account *forProfileLocked(const std::string &profile);
    void forgetLocked(const std::string &id);
    Json accountsLocked(const std::string &activeId) const;
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
    std::string deviceCode_, userCode_, verificationUrl_, signInClientId_, signInClientSecret_,
        signInProfile_;
    int64_t signInEndsMs_ = 0, nextPollMs_ = 0, pollGapMs_ = 5000;
    // Linked, in the order they were added, and each profile's choice.
    std::vector<Account> accounts_;
    std::map<std::string, std::string> profiles_;
    int nextId_ = 1;
    std::string error_;
    // The row being asked for.
    std::string rowWantedId_;
    bool rowBusy_ = false;
    std::string rowClientId_, rowClientSecret_;
};

} // namespace service
} // namespace nativetv
