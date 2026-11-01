// Fork: the Wallpaper Gallery, opened from Quick settings > Home settings > Wallpapers.
//
// Every picture the TV has in its gallery (fork-tools/prepare-wallpapers.py, copied over SSH) is a
// tile showing it as Automatic draws it: a landscape filling the tile, a portrait centred over its
// own blurred sides. OK or a click chooses one for Home; the first tile goes back to the
// time-of-day pictures. The choice is saved in the Home layout through the optimistic layer, so the
// mark moves at once and Home crossfades to the picture once the server has it. Only the tiles in
// view ask for their thumbnails, which are small files of their own, so a large gallery costs no
// more memory than the screenful shown.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/kit/ambience.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cstdlib>

namespace nativetv {
namespace {
using kit::easing;
using say::msg;

constexpr int kColumns = 4;
constexpr float kTileW = 352, kTileH = 198, kGapX = 64, kGapY = 76;
constexpr float kPitch = kTileH + kGapY;
constexpr float kLeft = (kit::canvas::width - (kColumns * kTileW + (kColumns - 1) * kGapX)) / 2;
constexpr float kTop = 336, kViewTop = 300, kViewBottom = kit::canvas::height - 24;
constexpr const char *kTimeOfDay = "slots";

Write wallpaperWrite() {
    Write write;
    write.operation = Operation::SetHomeLayout;
    write.body = [](const Json &target) {
        Json body = Json::object();
        body.set("wallpaper", target);
        return body;
    };
    write.readChannel = "catalogue";
    write.readOperation = Operation::HomeCatalogue;
    return write;
}

// "pexels-adnantemurbarcha-17913609" -> "pexels adnantemurbarcha 17913609"; NASA's "~orig" goes.
std::string tidyName(std::string name) {
    const size_t tilde = name.find('~');
    if (tilde != std::string::npos)
        name.erase(tilde);
    std::replace(name.begin(), name.end(), '-', ' ');
    std::replace(name.begin(), name.end(), '_', ' ');
    return name.empty() ? "Wallpaper" : name;
}

class WallpaperScreen final : public ScreenView {
    struct Tile {
        std::string id, name, image, note;
    };

  public:
    std::string name() const override {
        return "wallpapers";
    }
    void enter(ScreenHost &host) override {
        rebuild(host);
        focus_ = indexOf(current(host));
        focusMoved(host, true);
        Timeline timeline;
        Tween rise(1, 0, kit::motion::ms::base, easing(kit::motion::ease::out));
        for (size_t i = 0; i < tiles_.size() && i < 16; ++i) {
            const std::string id = "wp:e:" + std::to_string(i);
            host.motion().set(id, 1);
            timeline.add(id, rise.delayedBy(kit::motion::stagger::pageItem * static_cast<double>(i) + 60));
        }
        timeline.play(host.motion());
    }
    void leave(ScreenHost &host) override {
        host.motion().erasePrefix("wp:");
    }
    // A gallery copied while the page is open shows without reopening it.
    bool update(ScreenHost &host) override {
        if (revision(host) != revision_) {
            rebuild(host);
            focus_ = std::min(focus_, tiles_.size() - 1);
            focusMoved(host, true);
        }
        return false;
    }
    bool key(ScreenHost &host, Key key) override {
        const size_t count = tiles_.size();
        if (count == 0)
            return false;
        const size_t column = focus_ % kColumns, row = focus_ / kColumns;
        switch (key) {
        case Key::Left:
            if (column > 0)
                move(host, focus_ - 1);
            return true;
        case Key::Right:
            if (column + 1 < static_cast<size_t>(kColumns) && focus_ + 1 < count)
                move(host, focus_ + 1);
            return true;
        case Key::Up:
            if (row == 0)
                return false; // the bar takes the focus
            move(host, focus_ - kColumns);
            return true;
        case Key::Down:
            if (focus_ + kColumns < count)
                move(host, focus_ + kColumns);
            else if (row < (count - 1) / kColumns)
                move(host, count - 1);
            return true;
        case Key::PageUp:
            move(host, focus_ >= 2 * static_cast<size_t>(kColumns) ? focus_ - 2 * kColumns : column);
            return true;
        case Key::PageDown:
            move(host, std::min(count - 1, focus_ + 2 * kColumns));
            return true;
        case Key::Ok:
            choose(host, focus_);
            return true;
        default:
            return false;
        }
    }
    void focus(ScreenHost &host) override {
        focusMoved(host, false);
    }
    void click(ScreenHost &host, const std::string &id) override {
        if (id.compare(0, 8, "wp:tile:") != 0)
            return;
        const size_t index = static_cast<size_t>(std::atoi(id.c_str() + 8));
        if (index >= tiles_.size())
            return;
        move(host, index);
        choose(host, index);
    }

    void build(ScreenHost &host, kit::Paint paint) override {
        const Motion &motion = host.motion();
        Scene &scene = paint.scene;
        paint.glyphs.text(scene, {kit::canvas::safeX, 160, 1200, kit::type::headline.line},
                          msg("tv.wall.title", "Wallpapers"), kit::type::headline, kit::color::onSurface);
        paint.glyphs.text(scene, {kit::canvas::safeX, 232, 1600, kit::type::body.line}, summary(host),
                          kit::type::body, kit::color::onSurface2);
        const std::string chosen = current(host);
        const float scroll = static_cast<float>(motion.value("wp:sy"));
        const bool barFocused = host.barFocused();
        scene.pushClip({0, kViewTop, kit::canvas::width, kViewBottom - kViewTop});
        for (size_t i = 0; i < tiles_.size(); ++i) {
            const Tile &tile = tiles_[i];
            const Rect rest = {kLeft + (i % kColumns) * (kTileW + kGapX),
                               kTop + (i / kColumns) * kPitch + scroll, kTileW, kTileH};
            if (rest.y + rest.h < kViewTop - 60 || rest.y > kViewBottom + 60)
                continue;
            const std::string number = std::to_string(i);
            const double hidden = i < 16 ? motion.value("wp:e:" + number) : 0;
            kit::PosterCard card;
            card.rest = rest;
            card.id = "wp:tile:" + number;
            card.image = tile.image;
            card.title = tile.name;
            card.subtitle = tile.note;
            card.focus = barFocused ? 0 : static_cast<float>(motion.value("wp:f:" + number));
            card.enter = {static_cast<float>(1 - hidden), static_cast<float>(24 * hidden)};
            if (tile.image.empty()) {
                // The time-of-day tile has no picture: a clock on the card's own fill.
                kit::card(paint, card);
                const float scale = kit::focus::scaleAt(kit::zoomTarget(kit::motion::scale::card), card.focus);
                scene.pushOpacity(card.enter.alpha);
                scene.pushTransform(kit::focus::transformAt(rest, scale));
                paint.glyphs.icon(scene, {rest.x + rest.w / 2 - 28, rest.y + 44, 56, 56}, "clock",
                                  kit::color::onSurface2);
                scene.popTransform();
                scene.popOpacity();
            } else {
                kit::card(paint, card);
            }
            if (tile.id == chosen) {
                // The mark of the wallpaper Home shows, scaled with its card.
                const float scale = kit::focus::scaleAt(kit::zoomTarget(kit::motion::scale::card), card.focus);
                scene.pushOpacity(card.enter.alpha);
                scene.pushTransform(Transform::translate(0, card.enter.dy));
                scene.pushTransform(kit::focus::transformAt(rest, scale));
                const Rect badge = {rest.x + rest.w - 52, rest.y + 12, 40, 40};
                Node &mark = scene.shape(badge, kit::color::accent, 20);
                mark.shadow = {0, 4, 12, 0, {0, 0, 0, .45f}};
                paint.glyphs.icon(scene, {badge.x + 8, badge.y + 8, 24, 24}, "check", kit::color::onAccent);
                scene.popTransform();
                scene.popTransform();
                scene.popOpacity();
            }
        }
        scene.popClip();
    }
    Json snapshot() const override {
        Json value = Json::object();
        value.set("tiles", Json::numeric(static_cast<double>(tiles_.size())));
        value.set("selected", Json::numeric(static_cast<double>(focus_)));
        return value;
    }

  private:
    static std::string revision(const ScreenHost &host) {
        return host.view().catalogue["wallpapers"]["revision"].string();
    }
    void rebuild(ScreenHost &host) {
        const Json wallpapers = host.view().catalogue["wallpapers"];
        revision_ = revision(host);
        tiles_.clear();
        Tile timeOfDay;
        timeOfDay.id = kTimeOfDay;
        timeOfDay.name = wallpapers["available"].boolean() ? msg("tv.wall.timeOfDay", "Time of day")
                                                           : msg("tv.wall.none", "No picture");
        timeOfDay.note = wallpapers["available"].boolean()
                             ? msg("tv.wall.timeOfDayNote", "Morning, afternoon, evening and night")
                             : msg("tv.wall.noneNote", "The soft washes only");
        tiles_.push_back(timeOfDay);
        const std::string galleryRevision = wallpapers["galleryRevision"].string();
        for (const Json &item : wallpapers["gallery"].items()) {
            Tile tile;
            tile.id = item["id"].string();
            tile.name = tidyName(item["name"].string());
            tile.image = "wallpaper:" + tile.id + "-thumb" +
                         (galleryRevision.empty() ? "" : "@" + galleryRevision);
            const std::string kind = item["orientation"].string();
            tile.note = kind == "portrait"   ? msg("tv.wall.portrait", "Portrait")
                        : kind == "square" ? msg("tv.wall.square", "Square")
                                           : msg("tv.wall.landscape", "Landscape");
            tiles_.push_back(tile);
        }
        host.invalidate();
    }
    // What the server has; "" before anything was chosen, which shows the first picture.
    static std::string serverChoice(const ScreenHost &host) {
        return host.view().catalogue["layout"]["wallpaper"].string();
    }
    std::string current(ScreenHost &host) const {
        const std::string value = host.optimistic().text("layout.wallpaper", serverChoice(host));
        if (value == kTimeOfDay)
            return value;
        for (size_t i = 1; i < tiles_.size(); ++i)
            if (tiles_[i].id == value)
                return value;
        return tiles_.size() > 1 ? tiles_[1].id : kTimeOfDay;
    }
    size_t indexOf(const std::string &id) const {
        for (size_t i = 0; i < tiles_.size(); ++i)
            if (tiles_[i].id == id)
                return i;
        return 0;
    }
    std::string summary(ScreenHost &host) const {
        const Json layout = host.view().catalogue["layout"];
        const std::string mode = layout["wallpaperMode"].string();
        const std::string fit = mode == "fit"    ? msg("tv.wall.fit", "Fit")
                                : mode == "fill" ? msg("tv.wall.fill", "Fill")
                                : mode == "blur" ? msg("tv.wall.blur", "Blur background")
                                                 : msg("tv.wall.auto", "Automatic fit");
        const std::string rotate = layout["wallpaperRotate"].string();
        const std::string change =
            rotate == "daylight" ? msg("tv.wall.daylight", "Follows the time of day")
            : std::atoi(rotate.c_str()) > 0
                ? msg("tv.wall.rotates", "Changes every {n} min from the chosen one",
                      fields({{"n", Json::text(rotate)}}))
                : msg("tv.wall.stays", "Stays until you choose another");
        return msg("tv.wall.summary", "{count} pictures  ·  {fit}  ·  {change}",
                   fields({{"count", Json::text(std::to_string(tiles_.size() - 1))},
                           {"fit", Json::text(fit)},
                           {"change", Json::text(change)}}));
    }
    void move(ScreenHost &host, size_t index) {
        if (index == focus_ || index >= tiles_.size())
            return;
        focus_ = index;
        focusMoved(host, false);
    }
    void focusMoved(ScreenHost &host, bool jump) {
        Motion &motion = host.motion();
        for (size_t i = 0; i < tiles_.size(); ++i) {
            const std::string id = "wp:f:" + std::to_string(i);
            const double to = i == focus_ ? 1 : 0;
            if (jump)
                motion.set(id, to);
            else
                motion.tween(id, to, kit::motion::ms::focus, easing(kit::motion::ease::out));
        }
        // The focused row is kept second from the top once there is a row above it.
        const size_t rows = (tiles_.size() + kColumns - 1) / kColumns;
        const size_t row = focus_ / kColumns;
        const size_t first = row == 0 ? 0 : std::min(row - 1, rows > 2 ? rows - 2 : 0);
        const double target = -static_cast<double>(first) * kPitch;
        if (jump)
            motion.set("wp:sy", target);
        else
            motion.spring("wp:sy", target, kit::springParams(kit::motion::spring::list));
        host.invalidate();
    }
    void choose(ScreenHost &host, size_t index) {
        if (index >= tiles_.size())
            return;
        host.optimistic().set("layout.wallpaper", Json::text(tiles_[index].id),
                              Json::text(serverChoice(host)), wallpaperWrite());
        host.optimistic().commit("layout.wallpaper");
        host.invalidate();
    }

    std::vector<Tile> tiles_;
    size_t focus_ = 0;
    std::string revision_;
};
} // namespace

std::unique_ptr<ScreenView> makeWallpaperScreen(const std::string &route) {
    return route == "wallpapers" ? std::unique_ptr<ScreenView>(new WallpaperScreen()) : nullptr;
}
} // namespace nativetv
