// Fork: the YouTube account page, opened from Quick settings > Home settings > YouTube account.
//
// It signs a Google account in with the device flow: the TV shows a code and a QR code, the person
// enters the code at google.com/device on a phone and allows read-only YouTube access, and the
// server (youtube_account.cpp) finishes by itself. The page only shows the state the server reports,
// asking again every two seconds while a code is waiting and every ten otherwise. Signed in, Home
// gains "Latest from your subscriptions"; signing out revokes the access with Google.
//
// Each Home profile has its own account. Several can be signed in: the page lists them, switches
// the active profile to the next one, adds another, or signs the profile's one out.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/kit/entry.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace nativetv {
namespace {
using kit::easing;
using say::msg;

constexpr const char *kStatus = "yt:status", *kAction = "yt:act";
constexpr float kLeft = kit::canvas::safeX, kTextW = 1100;
constexpr double kConfirmMs = 5000;

class YouTubeScreen final : public ScreenView {
  public:
    std::string name() const override {
        return "youtube-account";
    }
    void enter(ScreenHost &host) override {
        ask(host);
        host.motion().set("yt:f", 1);
        host.motion().set("yt:e", 1);
        host.motion().tween("yt:e", 0, kit::motion::ms::base, easing(kit::motion::ease::out), 60);
    }
    void leave(ScreenHost &host) override {
        host.services().cancel(kStatus);
        host.services().cancel(kAction);
        host.motion().erasePrefix("yt:");
    }
    bool update(ScreenHost &host) override {
        const double now = host.nowMs();
        const double gap = status_["pending"].isObject() ? 2000 : 10000;
        if (!asking_ && now - askedAt_ >= gap)
            ask(host);
        if (confirmUntil_ > 0 && now >= confirmUntil_) {
            confirmUntil_ = 0;
            host.invalidate();
        }
        // The code's minutes count down on screen.
        return status_["pending"].isObject();
    }
    bool key(ScreenHost &host, Key key) override {
        if (key == Key::Up)
            return false;
        if (key == Key::Ok) {
            const std::vector<std::string> shown = buttons();
            if (!shown.empty())
                act(host, shown[std::min(selected_, shown.size() - 1)]);
            return true;
        }
        if (key == Key::Left || key == Key::Right) {
            const size_t count = buttons().size();
            if (count) {
                selected_ = std::min(selected_, count - 1);
                if (key == Key::Left && selected_ > 0)
                    --selected_;
                else if (key == Key::Right && selected_ + 1 < count)
                    ++selected_;
                confirmUntil_ = 0;
                host.invalidate();
            }
        }
        return key == Key::Down || key == Key::Left || key == Key::Right;
    }
    void focus(ScreenHost &host) override {
        host.motion().tween("yt:f", 1, kit::motion::ms::focus, easing(kit::motion::ease::out));
    }
    void click(ScreenHost &host, const std::string &id) override {
        const std::vector<std::string> shown = buttons();
        for (size_t i = 0; i < shown.size(); ++i)
            if (id == "yt:" + shown[i]) {
                selected_ = i;
                act(host, shown[i]);
            }
    }
    void response(ScreenHost &host, const Response &response) override {
        const Json data = response.json();
        if (response.channel == kStatus) {
            asking_ = false;
            if (response.ok() && data.isObject()) {
                status_ = data;
                loaded_ = true;
                if (!data["error"].string().empty())
                    error_ = data["error"].string();
                else if (data["linked"].boolean() || data["pending"].isObject())
                    error_.clear();
            } else if (!loaded_) {
                error_ = msg("tv.yt.noAnswer", "Native Tv did not answer. Trying again…");
            }
            host.invalidate();
        } else if (response.channel == kAction) {
            busy_ = false;
            if (!response.ok() || (data.isObject() && data.has("ok") && !data["ok"].boolean()))
                error_ = data["error"].string(msg("tv.yt.failed", "That did not work. Try again."));
            ask(host);
        }
    }

    void build(ScreenHost &host, kit::Paint paint) override {
        Scene &scene = paint.scene;
        const float hidden = static_cast<float>(host.motion().value("yt:e"));
        scene.pushOpacity(1 - hidden);
        scene.pushTransform(Transform::translate(0, 24 * hidden));
        paint.glyphs.text(scene, {kLeft, 160, kTextW, kit::type::headline.line},
                          msg("tv.yt.title", "YouTube account"), kit::type::headline, kit::color::onSurface);
        float y = 250;
        auto line = [&](const std::string &text, const kit::TypeStyle &style, Color ink, int lines = 1) {
            paint.glyphs.text(scene, {kLeft, y, kTextW, style.line * lines}, text, style, ink,
                              Node::Align::Left, lines);
            y += style.line * lines + 12;
        };
        if (!loaded_) {
            line(msg("tv.yt.checking", "Checking…"), kit::type::bodyL, kit::color::onSurface2);
        } else if (!status_["configured"].boolean()) {
            line(msg("tv.yt.setupTitle", "Native Tv needs your own Google sign-in client first."),
                 kit::type::bodyL, kit::color::onSurface);
            line(msg("tv.yt.setupBody",
                     "Create an OAuth client of the type \"TVs and Limited Input devices\" in Google Cloud, "
                     "with the YouTube Data API enabled, and put its youtubeClientId and youtubeClientSecret "
                     "in Native Tv's config.json."),
                 kit::type::body, kit::color::onSurface2, 3);
        } else if (status_["pending"].isObject()) {
            const Json pending = status_["pending"];
            line(msg("tv.yt.step", "On your phone or computer, go to"), kit::type::bodyL, kit::color::onSurface2);
            line("google.com/device", kit::type::headline, kit::color::accent);
            line(msg("tv.yt.enter", "and enter this code:"), kit::type::bodyL, kit::color::onSurface2);
            line(pending["code"].string(), kit::type::numeral, kit::color::onSurface);
            const int minutes = static_cast<int>(std::ceil(pending["expiresInMs"].number() / 60000));
            line(msg("tv.yt.expires", "The code works for {n} more minutes. This page follows by itself.",
                     fields({{"n", Json::text(std::to_string(std::max(1, minutes)))}})),
                 kit::type::body, kit::color::onSurface3);
            kit::QrTile tile;
            tile.rest = {1420, 300, 360, 360};
            tile.text = pending["url"].string("https://www.google.com/device");
            tile.sweep = -1;
            kit::qrTile(paint, tile);
        } else if (status_["linked"].boolean()) {
            line(msg("tv.yt.linkedAs", "This profile shows {name}'s subscriptions.",
                     fields({{"name", status_["account"]["name"]}})),
                 kit::type::bodyL, kit::color::onSurface);
            const int videos = static_cast<int>(status_["videos"].number());
            line(videos > 0 ? msg("tv.yt.videos", "{n} new videos from its subscriptions are on Home.",
                                  fields({{"n", Json::text(std::to_string(videos))}}))
                            : msg("tv.yt.fetching", "The newest videos from its subscriptions are on their way to Home."),
                 kit::type::body, kit::color::onSurface2);
            const std::vector<Json> accounts = status_["accounts"].items();
            if (accounts.size() > 1) {
                std::string names;
                for (const Json &account : accounts)
                    names += (names.empty() ? "" : "   ·   ") +
                             std::string(account["active"].boolean() ? "● " : "") + account["name"].string();
                line(msg("tv.yt.accounts", "Signed in: {names}", fields({{"names", Json::text(names)}})),
                     kit::type::body, kit::color::onSurface2, 2);
            }
            line(msg("tv.yt.scope",
                     "Native Tv reads only subscriptions. YouTube does not let any app see your history, "
                     "recommendations or where you stopped watching."),
                 kit::type::body, kit::color::onSurface3, 2);
        } else {
            line(msg("tv.yt.intro", "Sign in to show the newest videos from your subscriptions on Home."),
                 kit::type::bodyL, kit::color::onSurface);
            line(msg("tv.yt.readOnly",
                     "Read-only: Native Tv reads which channels you subscribe to. Your password never reaches the TV."),
                 kit::type::body, kit::color::onSurface2, 2);
        }
        if (!error_.empty())
            line(error_, kit::type::body, kit::color::warn, 2);
        const std::vector<std::string> shown = buttons();
        if (!shown.empty())
            selected_ = std::min(selected_, shown.size() - 1);
        float x = kLeft;
        for (size_t i = 0; i < shown.size(); ++i) {
            const std::string &action = shown[i];
            kit::PillButton pill;
            pill.id = "yt:" + action;
            if (action == "signin") {
                pill.label = msg("tv.yt.signIn", "Sign in with Google");
                pill.icon = "play";
                pill.primary = true;
            } else if (action == "switch") {
                pill.label = msg("tv.yt.switch", "Switch account");
                pill.icon = "arrowright";
                pill.primary = true;
            } else if (action == "add") {
                pill.label = msg("tv.yt.add", "Add another account");
                pill.icon = "play";
            } else {
                pill.label = confirmUntil_ > 0 ? msg("tv.yt.confirm", "Press OK again to sign out")
                                               : msg("tv.yt.signOut", "Sign out");
                pill.icon = "close";
            }
            if (busy_ && i == selected_)
                pill.label = msg("tv.yt.wait", "One moment…");
            const float width = action == "signout" && confirmUntil_ > 0 ? 560.0f : 440.0f;
            pill.rest = {x, std::max(y + 24, 560.0f), width, 72};
            pill.focus = host.barFocused() || i != selected_ ? 0 : static_cast<float>(host.motion().value("yt:f"));
            kit::pillButton(paint, pill);
            x += width + 32;
        }
        scene.popTransform();
        scene.popOpacity();
    }

  private:
    void ask(ScreenHost &host) {
        asking_ = true;
        askedAt_ = host.nowMs();
        host.services().read(kStatus, Operation::YouTubeAccount);
    }
    // The page's buttons, left to right, for what the server reports.
    std::vector<std::string> buttons() const {
        if (!loaded_ || !status_["configured"].boolean() || status_["pending"].isObject())
            return {};
        if (!status_["linked"].boolean())
            return {"signin"};
        if (status_["accounts"].size() > 1)
            return {"switch", "add", "signout"};
        return {"add", "signout"};
    }
    void act(ScreenHost &host, const std::string &action) {
        if (busy_ || !loaded_ || !status_["configured"].boolean() || status_["pending"].isObject())
            return;
        if (action == "signout") {
            if (confirmUntil_ <= 0) {
                confirmUntil_ = host.nowMs() + kConfirmMs;
                host.invalidate();
                return;
            }
            confirmUntil_ = 0;
            busy_ = true;
            host.services().write(kAction, Operation::YouTubeUnlink, Json::object());
        } else if (action == "switch") {
            busy_ = true;
            error_.clear();
            host.services().write(kAction, Operation::YouTubeUse, Json::object());
        } else {
            busy_ = true;
            error_.clear();
            host.services().write(kAction, Operation::YouTubeLink, Json::object());
        }
        host.invalidate();
    }

    Json status_ = Json::object();
    std::string error_;
    bool loaded_ = false, asking_ = false, busy_ = false;
    double askedAt_ = 0, confirmUntil_ = 0;
    size_t selected_ = 0;
};
} // namespace

std::unique_ptr<ScreenView> makeYouTubeScreen(const std::string &route) {
    return route == "youtube-account" ? std::unique_ptr<ScreenView>(new YouTubeScreen()) : nullptr;
}
} // namespace nativetv
