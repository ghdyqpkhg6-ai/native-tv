#include "service/youtube_account.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <iterator>
#include <limits>
#include <map>
#include <set>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <curl/curl.h>

namespace nativetv {
namespace service {
namespace {
const char *const kScope = "https://www.googleapis.com/auth/youtube.readonly";
const char *const kDeviceUrl = "https://oauth2.googleapis.com/device/code";
const char *const kTokenUrl = "https://oauth2.googleapis.com/token";
const char *const kRevokeUrl = "https://oauth2.googleapis.com/revoke";
const std::string kApi = "https://www.googleapis.com/youtube/v3/";
const int64_t kRowAgeMs = 2LL * 60 * 60 * 1000;
const int64_t kRowRetryMs = 15LL * 60 * 1000;
const size_t kChannels = 30, kRowSize = 16;
const size_t kMaximumBody = 2 * 1024 * 1024;
const size_t kMaximumFile = 1024 * 1024;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}
size_t appendBody(char *data, size_t size, size_t count, void *context) {
    if (size && count > std::numeric_limits<size_t>::max() / size)
        return 0;
    const size_t bytes = size * count;
    std::string *body = static_cast<std::string *>(context);
    if (body->size() + bytes > kMaximumBody)
        return 0;
    body->append(data, bytes);
    return bytes;
}
bool idCharacters(const std::string &text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
               c == '-';
    });
}
bool videoId(const std::string &id) {
    return id.size() == 11 && idCharacters(id);
}
bool channelId(const std::string &id) {
    return id.size() == 24 && id.compare(0, 2, "UC") == 0 && idCharacters(id);
}
// At most `maximum` bytes, never cutting a UTF-8 character in two.
std::string cut(const std::string &text, size_t maximum) {
    if (text.size() <= maximum)
        return text;
    size_t end = maximum;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    return text.substr(0, end);
}
// "PT1H2M3S" -> "1:02:03"; empty for a live stream or anything unexpected.
std::string videoLength(const std::string &iso) {
    if (iso.compare(0, 2, "PT") != 0)
        return "";
    long hours = 0, minutes = 0, seconds = 0, number = 0;
    bool digits = false;
    for (size_t i = 2; i < iso.size(); ++i) {
        const char c = iso[i];
        if (c >= '0' && c <= '9') {
            number = number * 10 + (c - '0');
            if (number > 100000)
                return "";
            digits = true;
            continue;
        }
        if (c == 'H')
            hours = number;
        else if (c == 'M')
            minutes = number;
        else if (c == 'S')
            seconds = number;
        else
            return "";
        number = 0;
    }
    if (!digits)
        return "";
    char text[64];
    if (hours)
        std::snprintf(text, sizeof(text), "%ld:%02ld:%02ld", hours, minutes, seconds);
    else
        std::snprintf(text, sizeof(text), "%ld:%02ld", minutes, seconds);
    return text;
}
std::string googleError(const Json &reply, const std::string &transport) {
    std::string message = reply["error_description"].string();
    if (message.empty() && reply["error"].isString())
        message = reply["error"].string();
    if (message.empty())
        message = reply["error"]["message"].string();
    if (message.empty())
        message = transport.empty() ? "Google did not answer." : transport;
    return cut(message, 200);
}
const char *const kMainProfile = "main";
// Home's profile ids are "main" and "p<n>" (home_profiles.cpp); anything else counts as the main one.
std::string profileKey(const std::string &profile) {
    if (profile.empty() || profile.size() > 32 || !idCharacters(profile))
        return kMainProfile;
    return profile;
}
bool accountKey(const std::string &id) {
    return id.size() >= 2 && id.size() <= 16 && id[0] == 'a' && idCharacters(id);
}
} // namespace

YouTubeAccount::YouTubeAccount(std::string dataRoot, Http http)
    : path_((dataRoot.empty() ? std::string("/var/lib/tvweb") : dataRoot) + "/youtube-account.json"),
      http_(std::move(http)) {
    worker_ = std::thread(&YouTubeAccount::workerLoop, this);
}

YouTubeAccount::~YouTubeAccount() {
    stopping_.store(true);
    changed_.notify_all();
    if (worker_.joinable())
        worker_.join();
}

YouTubeAccount::Account *YouTubeAccount::findLocked(const std::string &id) {
    for (Account &account : accounts_)
        if (account.id == id)
            return &account;
    return nullptr;
}

YouTubeAccount::Account *YouTubeAccount::forProfileLocked(const std::string &profile) {
    const auto chosen = profiles_.find(profileKey(profile));
    if (chosen != profiles_.end())
        if (Account *account = findLocked(chosen->second))
            return account;
    return accounts_.empty() ? nullptr : &accounts_.front();
}

void YouTubeAccount::forgetLocked(const std::string &id) {
    const std::string gone = id; // `id` may belong to the account being erased
    accounts_.erase(std::remove_if(accounts_.begin(), accounts_.end(),
                                   [&gone](const Account &account) { return account.id == gone; }),
                    accounts_.end());
    for (auto choice = profiles_.begin(); choice != profiles_.end();)
        choice = choice->second == gone ? profiles_.erase(choice) : std::next(choice);
    if (rowWantedId_ == gone)
        rowWantedId_.clear();
}

namespace {
// An account whose channel name has not arrived yet (or that has no channel) is named by its place.
std::string displayName(const std::string &name, size_t index) {
    return name.empty() ? "Account " + std::to_string(index + 1) : name;
}
} // namespace

Json YouTubeAccount::accountsLocked(const std::string &activeId) const {
    Json list = Json::array();
    for (size_t i = 0; i < accounts_.size(); ++i) {
        Json item = Json::object();
        item.set("id", Json::text(accounts_[i].id));
        item.set("name", Json::text(displayName(accounts_[i].name, i)));
        item.set("active", Json::flag(accounts_[i].id == activeId));
        list.add(item);
    }
    return list;
}

Json YouTubeAccount::status(const std::string &profile, const std::string &clientId,
                            const std::string &clientSecret) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_)
        loadLocked();
    const Account *account = forProfileLocked(profile);
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    result.set("configured", Json::flag(!clientId.empty() && !clientSecret.empty()));
    result.set("linked", Json::flag(account != nullptr));
    if (account) {
        const size_t index = static_cast<size_t>(account - accounts_.data());
        Json current = Json::object();
        current.set("id", Json::text(account->id));
        current.set("name", Json::text(displayName(account->name, index)));
        result.set("account", current);
    }
    result.set("accounts", accountsLocked(account ? account->id : ""));
    if (!deviceCode_.empty()) {
        Json pending = Json::object();
        pending.set("code", Json::text(userCode_));
        pending.set("url", Json::text(verificationUrl_));
        pending.set("expiresInMs", Json::numeric(static_cast<double>(std::max<int64_t>(0, signInEndsMs_ - nowMs()))));
        result.set("pending", pending);
    }
    result.set("error", Json::text(error_));
    result.set("videos", Json::numeric(account && account->row.isObject()
                                           ? static_cast<double>(account->row["items"].size())
                                           : 0));
    return result;
}

Json YouTubeAccount::link(const std::string &profile, const std::string &clientId,
                          const std::string &clientSecret) {
    Json result = Json::object();
    if (clientId.empty() || clientSecret.empty()) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text("Add the Google OAuth client to config.json first."));
        return result;
    }
    Json reply;
    long code = 0; // not `status`: that is the member this returns at the end
    std::string error;
    const bool ok = request(kDeviceUrl, "client_id=" + encode(clientId) + "&scope=" + encode(kScope), "",
                            reply, code, error);
    if (!ok || code != 200 || reply["device_code"].string().empty() || reply["user_code"].string().empty()) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text(googleError(reply, error)));
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const int64_t now = nowMs();
        deviceCode_ = reply["device_code"].string();
        userCode_ = cut(reply["user_code"].string(), 32);
        verificationUrl_ = reply["verification_url"].string(reply["verification_uri"].string("https://www.google.com/device"));
        if (verificationUrl_.compare(0, 8, "https://") != 0)
            verificationUrl_ = "https://www.google.com/device";
        signInEndsMs_ = now + static_cast<int64_t>(reply["expires_in"].number(1800)) * 1000;
        pollGapMs_ = std::max<int64_t>(5, static_cast<int64_t>(reply["interval"].number(5))) * 1000;
        nextPollMs_ = now + pollGapMs_;
        signInClientId_ = clientId;
        signInClientSecret_ = clientSecret;
        signInProfile_ = profileKey(profile);
        error_.clear();
    }
    changed_.notify_all();
    return status(profile, clientId, clientSecret);
}

Json YouTubeAccount::unlink(const std::string &profile, const std::string &accountId) {
    std::string token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!loaded_)
            loadLocked();
        error_.clear();
        if (!deviceCode_.empty()) {
            // Signing out while a code waits cancels the code; the accounts stay.
            deviceCode_.clear();
        } else if (Account *account = accountId.empty() ? forProfileLocked(profile) : findLocked(accountId)) {
            token = account->refreshToken;
            const std::string id = account->id;
            forgetLocked(id);
            saveLocked();
        }
    }
    if (!token.empty()) {
        // Best effort: the token is gone from the TV either way.
        Json reply;
        long status = 0;
        std::string error;
        (void)request(kRevokeUrl, "token=" + encode(token), "", reply, status, error);
    }
    Json result = Json::object();
    result.set("ok", Json::flag(true));
    return result;
}

Json YouTubeAccount::use(const std::string &profile, const std::string &accountId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_)
        loadLocked();
    Json result = Json::object();
    if (accounts_.empty()) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text("No YouTube account is signed in."));
        return result;
    }
    std::string target = accountId;
    if (target.empty()) {
        const Account *current = forProfileLocked(profile);
        const size_t index = static_cast<size_t>(current - accounts_.data());
        target = accounts_[(index + 1) % accounts_.size()].id;
    } else if (!findLocked(target)) {
        result.set("ok", Json::flag(false));
        result.set("error", Json::text("That account is no longer signed in."));
        return result;
    }
    profiles_[profileKey(profile)] = target;
    saveLocked();
    const Account *chosen = findLocked(target);
    Json account = Json::object();
    account.set("id", Json::text(chosen->id));
    account.set("name", Json::text(displayName(chosen->name, static_cast<size_t>(chosen - accounts_.data()))));
    result.set("ok", Json::flag(true));
    result.set("account", account);
    return result;
}

Json YouTubeAccount::subscriptionsRow(const std::string &profile, const std::string &clientId,
                                      const std::string &clientSecret) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!loaded_)
        loadLocked();
    Account *account = forProfileLocked(profile);
    if (!account)
        return Json();
    const int64_t now = nowMs();
    // One row is asked for at a time; another account's turn comes at Home's next read.
    if (now - account->rowAtMs >= kRowAgeMs && now - account->rowTriedMs >= kRowRetryMs &&
        rowWantedId_.empty() && !clientId.empty()) {
        rowWantedId_ = account->id;
        account->rowTriedMs = now;
        rowClientId_ = clientId;
        rowClientSecret_ = clientSecret;
        changed_.notify_all();
    }
    // A fresh object: the cached row is shared and must not gain the picker's fields.
    Json row = Json::object();
    row.set("key", Json::text("subs"));
    row.set("items", account->row.isObject() ? account->row["items"] : Json::array());
    row.set("account", Json::text(displayName(account->name, static_cast<size_t>(account - accounts_.data()))));
    row.set("accountId", Json::text(account->id));
    row.set("accounts", accountsLocked(account->id));
    return row;
}

void YouTubeAccount::workerLoop() {
    for (;;) {
        std::string clientId, clientSecret, device, accountId;
        bool poll = false, refresh = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            // A second's sleep is fine grain for the sign-in's five-second polls.
            changed_.wait_for(lock, std::chrono::seconds(1),
                              [this]() { return stopping_.load() || (!rowWantedId_.empty() && !rowBusy_); });
            if (stopping_.load())
                return;
            const int64_t now = nowMs();
            if (!deviceCode_.empty()) {
                if (now >= signInEndsMs_) {
                    deviceCode_.clear();
                    error_ = "The code expired. Choose Sign in to get a new one.";
                } else if (now >= nextPollMs_) {
                    poll = true;
                    device = deviceCode_;
                    clientId = signInClientId_;
                    clientSecret = signInClientSecret_;
                    nextPollMs_ = now + pollGapMs_;
                }
            }
            if (!poll && !rowWantedId_.empty() && !rowBusy_) {
                refresh = true;
                accountId = rowWantedId_;
                rowWantedId_.clear();
                rowBusy_ = true;
                clientId = rowClientId_;
                clientSecret = rowClientSecret_;
            }
        }
        if (poll) {
            pollSignIn(clientId, clientSecret, device);
        } else if (refresh) {
            refreshRow(accountId, clientId, clientSecret);
            std::lock_guard<std::mutex> lock(mutex_);
            rowBusy_ = false;
        }
    }
}

void YouTubeAccount::pollSignIn(const std::string &clientId, const std::string &clientSecret,
                                const std::string &deviceCode) {
    Json reply;
    long status = 0;
    std::string error;
    const bool ok = request(kTokenUrl,
                            "client_id=" + encode(clientId) + "&client_secret=" + encode(clientSecret) +
                                "&device_code=" + encode(deviceCode) +
                                "&grant_type=" + encode("urn:ietf:params:oauth:grant-type:device_code"),
                            "", reply, status, error);
    std::string added, token;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (deviceCode_ != deviceCode || !ok) // cancelled meanwhile, or no answer: the next poll asks again
            return;
        const std::string code = reply["error"].isString() ? reply["error"].string() : "";
        if (status == 200 && !reply["refresh_token"].string().empty()) {
            Account account;
            account.id = "a" + std::to_string(nextId_++);
            account.refreshToken = reply["refresh_token"].string();
            account.accessToken = reply["access_token"].string();
            account.accessEndsMs = nowMs() + (static_cast<int64_t>(reply["expires_in"].number(3600)) - 60) * 1000;
            account.rowTriedMs = nowMs();
            accounts_.push_back(account);
            profiles_[signInProfile_.empty() ? std::string(kMainProfile) : signInProfile_] = account.id;
            deviceCode_.clear();
            error_.clear();
            rowWantedId_ = account.id; // the row is fetched straight away
            rowClientId_ = clientId;
            rowClientSecret_ = clientSecret;
            saveLocked();
            added = account.id;
            token = account.accessToken;
        } else if (code == "authorization_pending") {
        } else if (code == "slow_down") {
            pollGapMs_ += 5000;
        } else {
            deviceCode_.clear();
            error_ = code == "access_denied"   ? "The sign-in was declined on the phone."
                     : code == "expired_token" ? "The code expired. Choose Sign in to get a new one."
                                               : "Google refused the sign-in: " + googleError(reply, error);
        }
    }
    if (!added.empty()) {
        // Named before the row is fetched, so the picker shows the channel rather than "Account 2".
        if (!token.empty())
            identify(added, token);
        changed_.notify_all();
    }
}

void YouTubeAccount::identify(const std::string &accountId, const std::string &token) {
    Json reply;
    long status = 0;
    std::string error;
    if (!request(kApi + "channels?part=snippet&mine=true", "", token, reply, status, error) || status != 200)
        return;
    const Json channel = reply["items"][static_cast<size_t>(0)];
    const std::string id = channel["id"].string();
    const std::string name = cut(channel["snippet"]["title"].string(), 80);
    std::lock_guard<std::mutex> lock(mutex_);
    Account *account = findLocked(accountId);
    if (!account)
        return;
    if (!name.empty())
        account->name = name;
    if (channelId(id))
        account->channelId = id;
    // The same channel signed in again: the earlier entry keeps its place and the profiles that
    // chose it, and takes the new token.
    std::string kept;
    if (!account->channelId.empty())
        for (Account &other : accounts_)
            if (&other != account && other.channelId == account->channelId) {
                other.refreshToken = account->refreshToken;
                other.accessToken = account->accessToken;
                other.accessEndsMs = account->accessEndsMs;
                other.name = account->name;
                kept = other.id;
                break;
            }
    if (!kept.empty()) {
        const std::string gone = account->id;
        const bool wanted = rowWantedId_ == gone;
        for (auto &choice : profiles_)
            if (choice.second == gone)
                choice.second = kept;
        forgetLocked(gone);
        if (wanted)
            rowWantedId_ = kept;
        if (Account *survivor = findLocked(kept))
            survivor->rowAtMs = survivor->rowTriedMs = 0; // fetched again for the renewed sign-in
    }
    saveLocked();
}

bool YouTubeAccount::accessToken(const std::string &accountId, const std::string &clientId,
                                 const std::string &clientSecret, std::string &token) {
    std::string refresh;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Account *account = findLocked(accountId);
        if (!account)
            return false;
        if (!account->accessToken.empty() && nowMs() < account->accessEndsMs) {
            token = account->accessToken;
            return true;
        }
        refresh = account->refreshToken;
    }
    Json reply;
    long status = 0;
    std::string error;
    const bool ok = request(kTokenUrl,
                            "client_id=" + encode(clientId) + "&client_secret=" + encode(clientSecret) +
                                "&refresh_token=" + encode(refresh) + "&grant_type=refresh_token",
                            "", reply, status, error);
    std::lock_guard<std::mutex> lock(mutex_);
    Account *account = findLocked(accountId);
    if (!account || account->refreshToken != refresh) // signed out or renewed meanwhile
        return false;
    if (ok && status == 200 && !reply["access_token"].string().empty()) {
        account->accessToken = reply["access_token"].string();
        account->accessEndsMs = nowMs() + (static_cast<int64_t>(reply["expires_in"].number(3600)) - 60) * 1000;
        token = account->accessToken;
        return true;
    }
    if (ok && reply["error"].isString() && reply["error"].string() == "invalid_grant") {
        // Revoked in the Google account, or the project's test sign-ins lapsed after seven days.
        const size_t index = static_cast<size_t>(account - accounts_.data());
        error_ = "Google ended the sign-in of " + displayName(account->name, index) + ". Choose Sign in again.";
        forgetLocked(accountId);
        saveLocked();
    }
    return false;
}

void YouTubeAccount::refreshRow(const std::string &accountId, const std::string &clientId,
                                const std::string &clientSecret) {
    std::string token;
    if (!accessToken(accountId, clientId, clientSecret, token))
        return;
    bool named = true;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const Account *account = findLocked(accountId);
        named = account && !account->name.empty();
    }
    if (!named) // an account from before names were kept, or one whose first ask failed
        identify(accountId, token);
    Json reply;
    long status = 0;
    std::string error;
    // The channels, most relevant first (YouTube's own order for the account).
    std::vector<std::string> channels;
    std::string page;
    for (int pass = 0; pass < 2 && channels.size() < kChannels; ++pass) {
        std::string url = kApi + "subscriptions?part=snippet&mine=true&maxResults=50&order=relevance";
        if (!page.empty())
            url += "&pageToken=" + encode(page);
        if (!request(url, "", token, reply, status, error) || status != 200)
            break;
        for (const Json &item : reply["items"].items()) {
            const std::string id = item["snippet"]["resourceId"]["channelId"].string();
            if (channelId(id) && std::find(channels.begin(), channels.end(), id) == channels.end())
                channels.push_back(id);
        }
        page = reply["nextPageToken"].string();
        if (page.empty())
            break;
    }
    // Each channel's newest uploads, from its uploads playlist ("UC..." -> "UU...").
    struct Upload {
        std::string id, title, channel, at;
    };
    std::vector<Upload> uploads;
    for (size_t i = 0; i < channels.size() && i < kChannels; ++i) {
        const std::string url = kApi + "playlistItems?part=snippet,contentDetails&maxResults=3&playlistId=UU" +
                                channels[i].substr(2);
        if (!request(url, "", token, reply, status, error) || status != 200)
            continue;
        for (const Json &item : reply["items"].items()) {
            Upload upload;
            upload.id = item["contentDetails"]["videoId"].string();
            upload.at = item["contentDetails"]["videoPublishedAt"].string(); // empty for a private video
            upload.title = item["snippet"]["title"].string();
            upload.channel = item["snippet"]["videoOwnerChannelTitle"].string(item["snippet"]["channelTitle"].string());
            if (videoId(upload.id) && !upload.at.empty())
                uploads.push_back(upload);
        }
    }
    std::sort(uploads.begin(), uploads.end(), [](const Upload &a, const Upload &b) { return a.at > b.at; });
    std::vector<Upload> newest;
    std::set<std::string> seen;
    for (const Upload &upload : uploads)
        if (newest.size() < kRowSize && seen.insert(upload.id).second)
            newest.push_back(upload);
    // Lengths, and which are still to come (premieres), in one call.
    std::map<std::string, std::string> lengths;
    std::set<std::string> upcoming;
    if (!newest.empty()) {
        std::string ids;
        for (const Upload &upload : newest)
            ids += (ids.empty() ? "" : ",") + upload.id;
        if (request(kApi + "videos?part=contentDetails,snippet&id=" + encode(ids), "", token, reply, status, error) &&
            status == 200)
            for (const Json &video : reply["items"].items()) {
                const std::string id = video["id"].string();
                lengths[id] = videoLength(video["contentDetails"]["duration"].string());
                if (video["snippet"]["liveBroadcastContent"].string() == "upcoming")
                    upcoming.insert(id);
            }
    }
    Json items = Json::array();
    for (const Upload &upload : newest) {
        if (upcoming.count(upload.id))
            continue;
        Json item = Json::object();
        item.set("id", Json::text(upload.id));
        item.set("type", Json::text("youtube"));
        item.set("title", Json::text(cut(upload.title, 240)));
        item.set("year", Json::text(""));
        item.set("rating", Json::parse("null"));
        item.set("overview", Json::text(cut(upload.channel, 160)));
        item.set("channel", Json::text(cut(upload.channel, 160)));
        item.set("duration", Json::text(lengths[upload.id]));
        item.set("published", Json::text(cut(upload.at, 40)));
        item.set("poster", Json::text(""));
        item.set("backdrop", Json::text(""));
        item.set("image", Json::text("https://i.ytimg.com/vi/" + upload.id + "/mqdefault.jpg"));
        item.set("wide", Json::text("https://i.ytimg.com/vi/" + upload.id + "/maxresdefault.jpg"));
        items.add(item);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    Account *account = findLocked(accountId);
    if (!account)
        return; // signed out meanwhile
    account->rowTriedMs = nowMs();
    if (items.size() == 0)
        return; // the last row stays; a failure is tried again in a quarter of an hour
    Json row = Json::object();
    row.set("key", Json::text("subs"));
    row.set("items", items);
    account->row = row;
    account->rowAtMs = nowMs();
    saveLocked();
}

bool YouTubeAccount::request(const std::string &url, const std::string &form, const std::string &bearer,
                             Json &reply, long &status, std::string &error) {
    reply = Json();
    status = 0;
    error.clear();
    if (http_)
        return http_(url, form, bearer, reply, status, error);
    static std::once_flag once;
    std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
    if (stopping_.load())
        return false;
    CURL *curl = curl_easy_init();
    if (!curl) {
        error = "Could not create a connection";
        return false;
    }
    std::string body;
    struct curl_slist *headers = nullptr;
    if (!bearer.empty())
        headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 15000L);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#pragma GCC diagnostic pop
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "tvweb");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    if (headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    if (!form.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, form.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
    }
    const CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);
    if (headers)
        curl_slist_free_all(headers);
    if (code != CURLE_OK) {
        error = curl_easy_strerror(code);
        return false;
    }
    reply = Json::parse(body); // empty (revoke) or an error page parses as nothing; status says enough
    return true;
}


void YouTubeAccount::loadLocked() {
    loaded_ = true;
    const int descriptor = open(path_.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return;
    struct stat info;
    std::string text;
    if (fstat(descriptor, &info) == 0 && S_ISREG(info.st_mode) && info.st_size > 0 &&
        static_cast<unsigned long long>(info.st_size) <= kMaximumFile) {
        char buffer[16384];
        for (;;) {
            const ssize_t count = read(descriptor, buffer, sizeof(buffer));
            if (count <= 0 || text.size() + static_cast<size_t>(count) > kMaximumFile)
                break;
            text.append(buffer, static_cast<size_t>(count));
        }
    }
    close(descriptor);
    const Json saved = Json::parse(text);
    if (!saved.isObject())
        return;
    const auto readRow = [](const Json &from, Account &account) {
        if (from["row"].isObject() && from["row"]["items"].isArray())
            account.row = from["row"];
        account.rowAtMs = static_cast<int64_t>(from["rowAt"].number());
    };
    if (!saved["accounts"].isArray()) {
        // The file from before accounts: one token, which becomes the first account.
        Account account;
        account.id = "a1";
        account.refreshToken = saved["refreshToken"].string();
        if (account.refreshToken.empty())
            return;
        readRow(saved, account);
        accounts_.push_back(account);
        nextId_ = 2;
        return;
    }
    int highest = 0;
    for (const Json &entry : saved["accounts"].items()) {
        Account account;
        account.id = entry["id"].string();
        account.refreshToken = entry["refreshToken"].string();
        if (!accountKey(account.id) || account.refreshToken.empty() || findLocked(account.id))
            continue;
        account.name = cut(entry["name"].string(), 80);
        account.channelId = entry["channelId"].string();
        if (!channelId(account.channelId))
            account.channelId.clear();
        readRow(entry, account);
        accounts_.push_back(account);
        highest = std::max(highest, std::atoi(account.id.c_str() + 1));
    }
    for (const std::string &profile : saved["profiles"].keys()) {
        const std::string id = saved["profiles"][profile].string();
        if (profileKey(profile) == profile && findLocked(id))
            profiles_[profile] = id;
    }
    nextId_ = std::max(highest + 1, static_cast<int>(saved["nextId"].number(1)));
    if (nextId_ < 1 || nextId_ > 1000000)
        nextId_ = highest + 1;
}

void YouTubeAccount::saveLocked() {
    if (accounts_.empty()) {
        (void)::unlink(path_.c_str());
        return;
    }
    Json list = Json::array();
    for (const Account &account : accounts_) {
        Json entry = Json::object();
        entry.set("id", Json::text(account.id));
        entry.set("name", Json::text(account.name));
        entry.set("channelId", Json::text(account.channelId));
        entry.set("refreshToken", Json::text(account.refreshToken));
        entry.set("row", account.row.isObject() ? account.row : Json::parse("null"));
        entry.set("rowAt", Json::numeric(static_cast<double>(account.rowAtMs)));
        list.add(entry);
    }
    Json profiles = Json::object();
    for (const auto &choice : profiles_)
        profiles.set(choice.first, Json::text(choice.second));
    Json saved = Json::object();
    saved.set("version", Json::numeric(2));
    saved.set("nextId", Json::numeric(nextId_));
    saved.set("accounts", list);
    saved.set("profiles", profiles);
    const std::string contents = saved.dump();
    const std::string temporary = path_ + ".tmp";
    const int descriptor = open(temporary.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (descriptor < 0)
        return;
    (void)fchmod(descriptor, 0600);
    size_t written = 0;
    bool ok = true;
    while (written < contents.size()) {
        const ssize_t count = write(descriptor, contents.data() + written, contents.size() - written);
        if (count <= 0) {
            ok = false;
            break;
        }
        written += static_cast<size_t>(count);
    }
    if (close(descriptor) != 0)
        ok = false;
    if (!ok || rename(temporary.c_str(), path_.c_str()) != 0)
        (void)::unlink(temporary.c_str());
}

} // namespace service
} // namespace nativetv
