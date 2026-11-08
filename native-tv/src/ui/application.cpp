#include "ui/application.hpp"
#include "ui/controller_helpers.hpp"
#include "ui/kit/ambience.hpp"
#include "ui/kit/components.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/home_layout.hpp"
#include "ui/splash.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace nativetv {
// Fork: the blurred sides of a centred gallery picture ("wallpaper:g07-fit@r" ->
// "wallpaper:g07-blur@r"), and where the centred picture sits on the 1920x1080 canvas.
std::string wallpaperBackdrop(const std::string &asset) {
    std::string backdrop = asset;
    const size_t at = backdrop.find("-fit");
    if (at != std::string::npos)
        backdrop.replace(at, 4, "-blur");
    return backdrop;
}
Rect wallpaperFitRect(const WallpaperLook &look) {
    const float w = look.fitW > 0 ? look.fitW : kit::canvas::width;
    const float h = look.fitH > 0 ? look.fitH : kit::canvas::height;
    return {(kit::canvas::width - w) / 2, (kit::canvas::height - h) / 2, w, h};
}
namespace {
constexpr uint32_t kCapsCoverMs = 2500;
// The size resolvePictures asks a wallpaper layer's picture at, so that waiting for it here and
// drawing it there share one decode.
bool wallpaperLoaded(Pictures &pictures, const std::string &asset, const WallpaperLook &look,
                     uint32_t now) {
    int width = 1600, height = 900;
    if (look.mode == "fit" || look.mode == "blur") {
        const Rect fit = wallpaperFitRect(look);
        if (fit.w < 1800) {
            width = pictureTexels(fit.w);
            height = pictureTexels(fit.h);
        }
    }
    if (pictures.view(asset, width, height, ImageFit::Cover, now).texture == 0)
        return false;
    return look.mode != "blur" ||
           pictures.view(wallpaperBackdrop(asset), 480, 270, ImageFit::Cover, now).texture != 0;
}
struct WallpaperChoice {
    std::string asset;
    float opacity = 0;
    WallpaperLook look;
};
// Fork: a gallery picture, chosen in the gallery or, with rotation on, the one the TV's clock has
// reached since it: every `minutes` the next one, from the chosen one on.
bool galleryWallpaperAt(const Json &catalogue, int month, int day, int hour, int minute,
                        WallpaperChoice &choice) {
    const Json layout = catalogue["layout"];
    const Json gallery = catalogue["wallpapers"]["gallery"];
    const std::string chosen = layout["wallpaper"].string();
    if (!gallery.isArray() || gallery.size() == 0 || chosen == "slots")
        return false;
    const std::vector<Json> items = gallery.items();
    size_t index = 0;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i]["id"].string() == chosen)
            index = i;
    const std::string rotate = layout["wallpaperRotate"].string();
    const int minutes = std::atoi(rotate.c_str());
    const long clock = ((static_cast<long>(month) * 31 + day) * 24 + hour) * 60 + minute;
    if (rotate == "daylight") {
        // Dynamic: a picture whose tone suits the hour, the next of them every half hour. Without
        // any of that tone, the nearest one (evening stands in for night and the other way round).
        const char *const day[] = {"day", "evening", "night"};
        const char *const evening[] = {"evening", "night", "day"};
        const char *const night[] = {"night", "evening", "day"};
        const char *const *tones = hour >= 6 && hour < 17 ? day : hour >= 17 && hour < 21 ? evening : night;
        std::vector<size_t> group;
        for (int t = 0; t < 3 && group.empty(); ++t)
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i]["tone"].string() == tones[t])
                    group.push_back(i);
        if (group.empty())
            for (size_t i = 0; i < items.size(); ++i)
                group.push_back(i);
        index = group[static_cast<size_t>(clock / 30) % group.size()];
    } else if (minutes > 0) {
        index = (index + static_cast<size_t>(clock / minutes)) % items.size();
    }
    const Json item = items[index];
    std::string mode = layout["wallpaperMode"].string();
    if (mode != "fit" && mode != "fill" && mode != "blur")
        mode = item["orientation"].string() == "landscape" ? "fill" : "blur";
    const std::string revision = catalogue["wallpapers"]["galleryRevision"].string();
    choice.asset = "wallpaper:" + item["id"].string() + (mode == "fill" ? "" : "-fit");
    if (!revision.empty())
        choice.asset += "@" + revision;
    // A picture chosen for itself shows more strongly than the time-of-day washes.
    choice.opacity = .82f;
    choice.look.mode = mode;
    choice.look.fitW = static_cast<float>(item["fitW"].number(kit::canvas::width));
    choice.look.fitH = static_cast<float>(item["fitH"].number(kit::canvas::height));
    return true;
}
WallpaperChoice wallpaperAt(const Json &catalogue, int hour) {
    static const struct {
        const char *slot;
        int from;
        float opacity;
    } slots[] = {{"night", 0, .55f}, {"morning", 6, .7f}, {"afternoon", 11, .62f},
                 {"evening", 17, .72f}, {"night", 21, .65f}};
    WallpaperChoice choice;
    Json wallpapers = catalogue["wallpapers"];
    if (!wallpapers["available"].boolean())
        return choice;
    int currentHour = hour < 0 ? 0 : hour % 24;
    const auto *slot = &slots[0];
    for (const auto &candidate : slots)
        if (candidate.from <= currentHour)
            slot = &candidate;
    Json available = wallpapers["slots"];
    if (available.isObject() &&
        (!available.has(slot->slot) || !available[slot->slot].boolean()))
        return choice;
    choice.asset = std::string("wallpaper:") + slot->slot;
    std::string revision = wallpapers["revision"].string();
    if (!revision.empty())
        choice.asset += "@" + revision;
    choice.opacity = slot->opacity;
    return choice;
}
std::string wallpaperSlot(const std::string &asset) {
    if (asset.compare(0, 10, "wallpaper:") != 0)
        return "";
    size_t end = asset.find('@', 10);
    return asset.substr(10, end == std::string::npos ? std::string::npos : end - 10);
}
} // namespace
void Application::wallClock(int hour, int minute, int weekday, int day, int month) {
    if (weekday < 0)
        weekday = state_.weekday;
    if (day < 0)
        day = state_.day;
    if (month < 0)
        month = state_.month;
    if (state_.hour != hour || state_.minute != minute || state_.weekday != weekday ||
        state_.day != day || state_.month != month) {
        state_.hour = hour;
        state_.minute = minute;
        state_.weekday = weekday;
        state_.day = day;
        state_.month = month;
        if (active_)
            syncWallpaper();
        dirty_ = true;
    }
}
Application::Application(Services &services, const std::string &initial)
    : services_(services), initial_(initial),
      clock_([this] { return static_cast<double>(now_); }),
      optimistic_(services, [this] { return clock_.nowMs(); },
                  [this](const std::string &message) { toast(message); }) {
    registerScreens(registry_);
    state_.home.tabs = homeTabs(Json());
    if (initial == "splash") {
        state_.screen = Screen::Splash;
        return;
    }
    services_.read("caps", Operation::Capabilities);
    capsAt_ = 2000;
    route(initial);
}
void Application::toast(const std::string &value) {
    state_.toast = value;
    toastUntil_ = now_ + 4000;
    motion_.play("toast", Tween(0, 1, kit::motion::ms::fast, Easing::out()));
    dirty_ = true;
}
// A server that was not ready when the client opened is asked again, so a fresh install still
// reaches setup, and the ink cover goes once the question has had its 2.5 s.
void Application::askCaps() {
    if (capsAnswered_)
        return;
    if (static_cast<int32_t>(now_ - capsAt_) >= 0) {
        services_.read("caps", Operation::Capabilities);
        capsAt_ = now_ + 2000;
    }
    if (holdForCaps_ && now_ >= kCapsCoverMs && now_ < kCapsCoverMs + 100)
        dirty_ = true;
}
bool Application::coveredForCaps() const {
    return holdForCaps_ && !capsAnswered_ && now_ < kCapsCoverMs;
}
void Application::active(bool value) {
    active_ = value;
    if (value) {
        capsAt_ = std::min(capsAt_, now_ + 300);
        if (state_.screen != Screen::Splash) {
            syncWallpaper();
            refresh();
        }
    } else {
        clearPicture();
    }
    dirty_ = true;
}
bool Application::update(uint32_t now) {
    now_ = now;
    if (!active_)
        return false;
    if (state_.screen == Screen::Splash)
        return true;
    clock_.tick();
    bool moved = motion_.update(clock_);
    keepAtmosphere();
    updateWallpaperTransition();
    if (static_cast<int32_t>(now_ - refreshAt_) >= 0)
        refresh();
    askCaps();
    if (toastUntil_ && static_cast<int32_t>(now_ - toastUntil_) >= 0) {
        toastUntil_ = 0;
        state_.toast.clear();
        dirty_ = true;
    }
    idleUpdate();
    // The layer sends what is due and takes back what timed out; a pending write keeps frames coming.
    bool changedByWrites = optimistic_.tick();
    bool screenBusy = optimistic_.active() || changedByWrites;
    if (state_.screen == Screen::Home || isRecents()) {
        homeUpdate();
        homeBeat(nullptr);
    }
    if (screen_)
        screenBusy = screen_->update(*this);
    // The wallpaper is shown once its picture has arrived.
    std::string pending = wallpaperPending();
    if (!pending.empty() && pictures_ &&
        wallpaperLoaded(*pictures_, pending, state_.home.wallpaperTargetLook, now_))
        imageReady(pending);
    bool draw = dirty_ || moved || state_.home.wallpaperTransition || screenBusy;
    ambientOnly_ = moved && motion_.ambientOnly() && !dirty_ && !state_.home.wallpaperTransition &&
                   !screenBusy;
    dirty_ = false;
    return draw;
}
// ---- Idle dimming -----------------------------------------------------------------------------
// The panel is an OLED, so no screen may sit still at full brightness: after ms::idleAfter
// without a key or click every screen but the splash falls to two thirds black over ms::idleDim.
// The first input afterwards only wakes it. Home's hero advance and the beat read `idle` too.
void Application::idleUpdate() {
    const uint32_t idleAt = flow_.lastKeyAt + static_cast<uint32_t>(kit::motion::ms::idleAfter);
    if (!flow_.idle && static_cast<int32_t>(now_ - idleAt) >= 0) {
        flow_.idle = true;
        motion_.tween("dim", .66, kit::motion::ms::idleDim, kit::easing(kit::motion::ease::inOut));
    }
}
// Fork: the animation level from Home's layout, read once per frame.
kit::MotionLevel Application::motionLevel() const {
    return kit::motionLevelOf(state_.catalogue["layout"]["motion"].string());
}
// The aurora, the motes and the wallpaper drift move by the frame clock, so they need frames: an
// ambient tween keeps the loop at its half rate while they are on screen. Nothing runs while the
// client is hidden (update() returns before this), once the screen has dimmed for idleness, with
// the level Off, or under the splash and setup.
void Application::keepAtmosphere() {
    const bool wanted = motionLevel() != kit::MotionLevel::Off && !flow_.idle &&
                        state_.screen != Screen::Splash && state_.screen != Screen::Setup &&
                        !coveredForCaps();
    if (!wanted || motion_.moving("atmosphere"))
        return;
    motion_.ambient("atmosphere");
    motion_.play("atmosphere", Tween(0, 1, 4000, Easing::linear()));
}
// Fork: a page opened over Home, or Home coming back from one, rises in from the ink.
void Application::playPageEnter() {
    if (motionLevel() == kit::MotionLevel::Off)
        return;
    const bool full = motionLevel() == kit::MotionLevel::Full;
    motion_.play("pagein", Tween(1, 0, full ? 340 : 240, kit::easing(kit::motion::ease::out)));
}
void Application::drawPageEnter(kit::Paint &paint) {
    float hidden = static_cast<float>(motion_.value("pagein"));
    if (hidden > .002f)
        paint.scene.shape({0, 0, kit::canvas::width, kit::canvas::height},
                          kit::focus::withAlpha(kit::color::ink, .9f * hidden));
}
// Input of any kind: true when the screen was dimmed, so that this one only woke it.
bool Application::wake() {
    flow_.lastKeyAt = now_;
    if (!flow_.idle)
        return false;
    flow_.idle = false;
    motion_.tween("dim", 0, kit::motion::ms::idleWake, kit::easing(kit::motion::ease::out));
    dirty_ = true;
    return true;
}
// Above everything, the toast and the fault banner too: a static banner burns in like the rest.
void Application::drawIdleDim(kit::Paint &paint) {
    float dim = static_cast<float>(motion_.value("dim"));
    if (dim > .002f)
        paint.scene.shape({0, 0, kit::canvas::width, kit::canvas::height}, {0, 0, 0, dim});
}
void Application::attach(kit::Glyphs &glyphs, Pictures &pictures) {
    glyphs_ = &glyphs;
    pictures_ = &pictures;
    flow_.tabTitles.clear(); // the tabs' widths come from the glyphs that draw them
    dirty_ = true;
}
void Application::syncWallpaper() {
    if (!active_ || !state_.catalogue.valid())
        return;
    WallpaperChoice choice;
    if (!galleryWallpaperAt(state_.catalogue, state_.month, state_.day, state_.hour, state_.minute,
                            choice))
        choice = wallpaperAt(state_.catalogue, state_.hour);
    auto &home = state_.home;
    if (home.wallpaperTarget == choice.asset && home.wallpaperTargetLook == choice.look &&
        std::fabs(home.wallpaperTargetOpacity - choice.opacity) < .001f)
        return;
    if (home.wallpaperTransition) {
        updateWallpaperTransition();
        if (home.wallpaperTransition)
            finishWallpaperTransition(home.wallpaperProgress >= .5f);
    }
    if (!home.wallpaperTarget.empty() && home.wallpaperTarget != choice.asset &&
        home.wallpaperTarget != home.wallpaperShown)
        services_.cancel("image:" + home.wallpaperTarget);
    home.wallpaperTarget = choice.asset;
    home.wallpaperTargetOpacity = choice.opacity;
    home.wallpaperTargetLook = choice.look;
    home.wallpaperFailed = false;
    if (home.wallpaperTarget.empty()) {
        if (!home.wallpaperShown.empty())
            startWallpaperTransition();
        else {
            home.wallpaperTransition = false;
            home.wallpaperFrom.clear();
            home.wallpaperProgress = 1;
        }
    } else {
        home.wallpaperTransition = false;
        home.wallpaperFrom.clear();
        home.wallpaperProgress = 1;
    }
    dirty_ = true;
}
void Application::startWallpaperTransition() {
    auto &home = state_.home;
    if (home.wallpaperTarget == home.wallpaperShown &&
        home.wallpaperTargetLook == home.wallpaperShownLook &&
        std::fabs(home.wallpaperTargetOpacity - home.wallpaperShownOpacity) < .001f)
        return;
    home.wallpaperFrom = home.wallpaperShown;
    home.wallpaperFromOpacity = home.wallpaperShownOpacity;
    home.wallpaperFromLook = home.wallpaperShownLook;
    home.wallpaperProgress = 0;
    home.wallpaperTransitionAt = now_;
    home.wallpaperTransition = true;
    dirty_ = true;
}
void Application::finishWallpaperTransition(bool chooseTarget) {
    auto &home = state_.home;
    if (chooseTarget) {
        home.wallpaperShown = home.wallpaperTarget;
        home.wallpaperShownOpacity = home.wallpaperTargetOpacity;
        home.wallpaperShownLook = home.wallpaperTargetLook;
    } else {
        home.wallpaperShown = home.wallpaperFrom;
        home.wallpaperShownOpacity = home.wallpaperFromOpacity;
        home.wallpaperShownLook = home.wallpaperFromLook;
    }
    home.wallpaperFrom.clear();
    home.wallpaperTransition = false;
    home.wallpaperProgress = 1;
    dirty_ = true;
}
void Application::updateWallpaperTransition() {
    auto &home = state_.home;
    if (!home.wallpaperTransition)
        return;
    uint32_t elapsed = now_ - home.wallpaperTransitionAt;
    if (elapsed >= 2400) {
        finishWallpaperTransition(true);
        return;
    }
    // The browser's default ease, as the Home page cross-faded its wallpaper.
    home.wallpaperProgress = static_cast<float>(Easing::ease()(elapsed / 2400.0));
}
void Application::imageReady(const std::string &path) {
    if (path.compare(0, 10, "wallpaper:") != 0)
        return;
    auto &home = state_.home;
    if (path != home.wallpaperTarget)
        return;
    home.wallpaperFailed = false;
    if (active_)
        startWallpaperTransition();
    dirty_ = true;
}
std::string Application::wallpaperPending() const {
    const auto &home = state_.home;
    if (!home.wallpaperTransition && !home.wallpaperTarget.empty() &&
        (home.wallpaperTarget != home.wallpaperShown ||
         home.wallpaperTargetLook != home.wallpaperShownLook ||
         std::fabs(home.wallpaperTargetOpacity - home.wallpaperShownOpacity) >= .001f))
        return home.wallpaperTarget;
    return "";
}
void Application::imageFailed(const std::string &path) {
    if (path.compare(0, 10, "wallpaper:") != 0 || path != state_.home.wallpaperTarget)
        return;
    state_.home.wallpaperFailed = true;
    dirty_ = true;
}
bool Application::isRecents() const {
    return state_.screen == Screen::Quick && state_.menu.level == "recents";
}
// Pictures are asked for at the size they are drawn at; a node whose picture has not arrived
// stays invisible (the kit draws a placeholder under it) and fades in when it has.
void Application::resolvePictures() {
    if (!pictures_)
        return;
    const double now = static_cast<double>(now_);
    for (size_t index = 0; index < scene_.nodes.size(); ++index) {
        Node &node = scene_.nodes[index];
        if (node.kind != Node::Kind::Image || node.texture || node.value.empty() ||
            node.value.compare(0, 6, "local:") == 0 || node.value.compare(0, 3, "qr:") == 0)
            continue;
        bool wide = node.rect.w >= 1800;
        // A wallpaper's blurred sides are small on purpose: the GPU stretches them for nothing.
        const bool backdrop = node.value.compare(0, 10, "wallpaper:") == 0 &&
                              node.value.find("-blur") != std::string::npos;
        int width = backdrop ? 480 : wide ? 1600 : pictureTexels(node.rect.w),
            height = backdrop ? 270 : wide ? 900 : pictureTexels(node.rect.h);
        ImageView view = pictures_->view(node.value, width, height,
                                         node.contain ? ImageFit::Contain : ImageFit::Cover, now);
        node.value.clear();
        if (view.texture) {
            node.texture = view.texture;
            node.opacity *= view.fade;
            if (view.opaque && index && scene_.nodes[index - 1].backing)
                scene_.nodes[index - 1].opacity = 0;
        } else
            node.opacity = 0;
    }
}
void Application::buildFrame() {
    // Fork: Home's style (Home settings > Appearance), a choice still being saved already counts.
    // Home's rows depend on it (Ribbon keeps only the apps), so a change rebuilds them.
    const home::Style style =
        home::styleOf(optimistic_.text("layout.style", state_.catalogue["layout"]["style"].string()));
    if (style != home::style()) {
        home::setStyle(style);
        rebuildHome();
    }
    scene_.reset();
    kit::setMotionLevel(motionLevel());
    kit::Paint paint{scene_, *glyphs_};
    Scene &scene = scene_;
    Frame frame{state_, motion_, static_cast<double>(now_)};
    if (state_.screen == Screen::Splash) {
        drawSplash(paint, now_);
        return;
    }
    // The first frames wait for the server's word on first-run setup (see capsAnswered_).
    if (coveredForCaps()) {
        scene.background = kit::color::ink;
        return;
    }
    if (screen_) {
        scene.background = kit::color::ink;
        const bool ownWall = screen_->paintsWall();
        if (!ownWall) {
            kit::ambientWall(paint, 1, state_.home.art.wallTint);
            kit::aurora(paint, now_ / 1000.0, .8f, state_.home.art.wallTint);
        }
        // The page rises the last few pixels as it comes in from the ink (playPageEnter).
        const float rise = ownWall ? 0 : 22 * static_cast<float>(motion_.value("pagein"));
        if (rise > .05f)
            scene.pushTransform(Transform::translate(0, rise));
        screen_->build(*this, paint);
        if (rise > .05f)
            scene.popTransform();
        if (screen_->hasBar())
            drawTopBar(paint, frame, screen_->barTab());
        if (!ownWall)
            drawPageEnter(paint);
    } else if (state_.screen == Screen::Home) {
        drawHome(paint, frame);
        drawPageEnter(paint);
    } else if (isRecents()) {
        drawHome(paint, frame);
        drawRecents(paint, frame);
    } else if (state_.screen == Screen::Kit)
        kit::gallery(scene, state_.kitComponent);
    else
        drawMenu(scene, state_);
    float toast = static_cast<float>(motion_.value("toast"));
    if (!state_.toast.empty() && toast > .002f) {
        scene.pushOpacity(toast);
        scene.pushTransform(Transform::translate(0, 16 * (1 - toast)));
        scene.shape({350, 940, 1220, 100}, kit::color::surface3, 20);
        paint.glyphs.text(scene, {382, 968, 1156, kit::type::title.line}, state_.toast,
                          kit::type::title, kit::color::onSurface);
        scene.popTransform();
        scene.popOpacity();
    }
    drawIdleDim(paint);
}
const Scene &Application::frame() {
    buildFrame();
    resolvePictures();
    return scene_;
}
// Scripted checks read the words on screen; with the glyph atlas they are glyph quads, so the
// frame is built once more with the plain text nodes.
bool Application::shows(const std::string &text) {
    kit::Glyphs *attached = glyphs_;
    glyphs_ = &legacyGlyphs_;
    Pictures *pictures = pictures_;
    pictures_ = nullptr;
    buildFrame();
    glyphs_ = attached;
    pictures_ = pictures;
    for (const auto &node : scene_.nodes)
        if (node.kind == Node::Kind::Text && node.value.find(text) != std::string::npos)
            return true;
    return false;
}
std::string Application::screenName() const {
    if (screen_)
        return screen_->name();
    switch (state_.screen) {
    case Screen::Splash:
        return "splash";
    case Screen::Home:
        return "home";
    case Screen::Dashboard:
        return "dashboard:" + dashboardTabs()[state_.menu.tab].key;
    case Screen::Quick:
        return "quick:" + state_.menu.level;
    case Screen::Setup:
        return "setup:" + state_.menu.level;
    case Screen::Catalog:
        return "catalog";
    case Screen::Kit:
        return "kit:" + state_.kitComponent;
    case Screen::Qr:
        return "qr";
    }
    return "home";
}
Json Application::snapshot() const {
    if (screen_) {
        Json value = screen_->snapshot();
        value.set("idle", Json::flag(flow_.idle));
        value.set("screen", Json::text(screen_->name()));
        value.set("barFocus", Json::flag(barFocus_));
        return value;
    }
    Json value = fields({{"screen", Json::text(screenName())},
                         {"rows", Json::numeric(state_.home.rows.size())},
                         {"busy", Json::flag(state_.busy)}});
    if (state_.screen == Screen::Quick && state_.menu.level == "recents") {
        value.set("recentApps", Json::numeric(state_.recents.cards.size()));
        value.set("selected", Json::numeric(state_.recents.selected));
        value.set("recentFocus",
                  Json::text(state_.recents.focus == RecentState::Focus::All     ? "all"
                             : state_.recents.focus == RecentState::Focus::Close ? "close"
                                                                                 : "card"));
    } else if (state_.screen == Screen::Home) {
        value.set("picture", Json::text(pictureLabel(state_.home.picture)));
        value.set("row", Json::numeric(state_.home.row));
        value.set("column", Json::numeric(state_.home.column));
        const auto &home = state_.home;
        value.set("tab", Json::text(home.tabs.empty() ? "" : home.tabs[home.tab].key));
        value.set("navigation", Json::flag(home.navigation));
        value.set("nav", Json::numeric(home.nav));
        value.set("arranging", Json::flag(home.arranging));
        value.set("editing", Json::flag(home.editing));
        value.set("editHide", Json::flag(home.editHide));
        value.set("focusCard", Json::text(home.row < home.rows.size() &&
                                                  home.column < home.rows[home.row].cards.size()
                                              ? home.rows[home.row].cards[home.column].id
                                              : ""));
        std::string rowHead;
        if (home.row < home.rows.size())
            for (size_t i = 0; i < home.rows[home.row].cards.size() && i < 3; ++i)
                rowHead += (rowHead.empty() ? "" : ",") + home.rows[home.row].cards[i].id;
        value.set("rowHead", Json::text(rowHead));
        value.set("rowCards", Json::numeric(home.row < home.rows.size()
                                                ? home.rows[home.row].cards.size()
                                                : 0));
        value.set("rowGroup",
                  Json::text(home.row < home.rows.size() ? home.rows[home.row].group : ""));
        value.set("heroTitle", Json::text(home.art.text.title));
        value.set("slides", Json::numeric(home.spotlight.size()));
        value.set("idle", Json::flag(flow_.idle));
        value.set("silent", Json::flag(state_.serverSilent));
        value.set("wallpaper", Json::text(
                                    home.wallpaperTransition
                                        ? "transitioning"
                                        : !home.wallpaperShown.empty()
                                              ? "visible"
                                              : home.wallpaperFailed
                                                    ? "failed-black"
                                                    : !home.wallpaperTarget.empty() ? "loading-black"
                                                                                   : "black"));
        value.set("wallpaperSlot", Json::text(wallpaperSlot(
                                        home.wallpaperTarget.empty() ? home.wallpaperShown
                                                                     : home.wallpaperTarget)));
    } else if (state_.menu.selected < state_.menu.rows.size())
        value.set("focus", Json::text(state_.menu.rows[state_.menu.selected].label));
    return value;
}
} // namespace nativetv
