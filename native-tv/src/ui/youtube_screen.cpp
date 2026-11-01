// Fork: the YouTube account page, opened from Quick settings > Home settings > YouTube account.
//
// It signs a Google account in with the device flow: the TV shows a code and a QR code, the person
// enters the code at google.com/device on a phone and allows read-only YouTube access, and the
// server (youtube_account.cpp) finishes by itself. The page only shows the state the server reports,
// asking again every two seconds while a code is waiting and every ten otherwise. Signed in, Home
// gains "Latest from your subscriptions"; signing out revokes the access with Google.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/kit/entry.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cmath>

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
            act(host);
            return true;
        }
        return key == Key::Down || key == Key::Left || key == Key::Right;
    }
    void focus(ScreenHost &host) override {
        host.motion().tween("yt:f", 1, kit::motion::ms::focus, easing(kit::motion::ease::out));
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (id == "yt:button")
            act(host);
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
        std::string button;
        bool primary = false;
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
            line(msg("tv.yt.linked", "Signed in to YouTube."), kit::type::bodyL, kit::color::onSurface);
            const int videos = static_cast<int>(status_["videos"].number());
            line(videos > 0 ? msg("tv.yt.videos", "{n} new videos from your subscriptions are on Home.",
                                  fields({{"n", Json::text(std::to_string(videos))}}))
                            : msg("tv.yt.fetching", "The newest videos from your subscriptions are on their way to Home."),
                 kit::type::body, kit::color::onSurface2);
            line(msg("tv.yt.scope",
                     "Native Tv reads only your subscriptions. YouTube does not let any app see your history, "
                     "recommendations or where you stopped watching."),
                 kit::type::body, kit::color::onSurface3, 2);
            button = confirmUntil_ > 0 ? msg("tv.yt.confirm", "Press OK again to sign out")
                                       : msg("tv.yt.signOut", "Sign out");
        } else {
            line(msg("tv.yt.intro", "Sign in to show the newest videos from your subscriptions on Home."),
                 kit::type::bodyL, kit::color::onSurface);
            line(msg("tv.yt.readOnly",
                     "Read-only: Native Tv reads which channels you subscribe to. Your password never reaches the TV."),
                 kit::type::body, kit::color::onSurface2, 2);
            button = msg("tv.yt.signIn", "Sign in with Google");
            primary = true;
        }
        if (!error_.empty())
            line(error_, kit::type::body, kit::color::warn, 2);
        if (!button.empty()) {
            kit::PillButton pill;
            pill.rest = {kLeft, std::max(y + 24, 560.0f), 520, 72};
            pill.id = "yt:button";
            pill.label = busy_ ? msg("tv.yt.wait", "One moment…") : button;
            pill.icon = primary ? "play" : "close";
            pill.primary = primary;
            pill.focus = host.barFocused() ? 0 : static_cast<float>(host.motion().value("yt:f"));
            kit::pillButton(paint, pill);
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
    void act(ScreenHost &host) {
        if (busy_ || !loaded_ || !status_["configured"].boolean() || status_["pending"].isObject())
            return;
        if (status_["linked"].boolean()) {
            if (confirmUntil_ <= 0) {
                confirmUntil_ = host.nowMs() + kConfirmMs;
                host.invalidate();
                return;
            }
            confirmUntil_ = 0;
            busy_ = true;
            host.services().write(kAction, Operation::YouTubeUnlink, Json::object());
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
};
} // namespace

std::unique_ptr<ScreenView> makeYouTubeScreen(const std::string &route) {
    return route == "youtube-account" ? std::unique_ptr<ScreenView>(new YouTubeScreen()) : nullptr;
}
} // namespace nativetv
