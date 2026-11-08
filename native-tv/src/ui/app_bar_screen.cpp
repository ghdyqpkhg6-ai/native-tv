// Fork: Home settings > Appearance > App bar style: the nine app bar designs (home_layout.hpp,
// home_bar.cpp) and Off, as a grid of cards. Each card shows a small drawing of its design made
// from the TV's own first apps, its name and what sets it apart; OK applies it to the active
// profile's Home, which shows it at once, and goes back to Home.
#include "domain/say.hpp"
#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/overlay.hpp"
#include "ui/kit/tuning.hpp"
#include "ui/optimistic.hpp"
#include "ui/overlay_routes.hpp"
#include <algorithm>
#include <cmath>

namespace nativetv {
namespace {
using kit::easing;
using say::msg;

struct Design {
    const char *key, *title, *blurb;
};
const Design kDesigns[] = {
    {"off", "Off", "Home as the style draws it"},
    {"aurora", "Aurora Glass Bar", "Frosted glass, rounded, soft glow"},
    {"horizon", "Horizon Ribbon", "Full-width bar with a gradient"},
    {"cards", "Floating Cards", "Large cards, the focus lifts forward"},
    {"neon", "Neon Edge", "Neon outlines, accent and depth"},
    {"circles", "Circular Dock", "Clean circular icons, minimalist"},
    {"vertical", "Vertical Stack", "Side bar, the view kept wide"},
    {"halo", "Halo Carousel", "Centred carousel, focus zoom"},
    {"fade", "Minimal Fade Bar", "Ultra-clean, fades when idle"},
    {"pills", "Glass Pill Tabs", "Categories in a glass pill"},
};
constexpr size_t kCount = sizeof(kDesigns) / sizeof(kDesigns[0]);
constexpr size_t kColumns = 5;
constexpr float kCardW = 320, kCardH = 180, kGapX = 34, kGapY = 132, kTop = 230;
constexpr Color kWhite = {1, 1, 1, 1};

Color faded(Color color, float amount) {
    color.a *= amount;
    return color;
}
Rect cardRect(size_t index) {
    const float total = kColumns * kCardW + (kColumns - 1) * kGapX;
    const float left = (kit::canvas::width - total) / 2;
    return {left + static_cast<float>(index % kColumns) * (kCardW + kGapX),
            kTop + static_cast<float>(index / kColumns) * (kCardH + kGapY), kCardW, kCardH};
}

// A small drawing of a design: a dusk sky and the TV's first apps laid out its way.
void preview(kit::Paint paint, Rect area, const std::string &key, const std::vector<Card> &apps) {
    Scene &scene = paint.scene;
    Node &sky = scene.shape(area, {.1f, .08f, .25f, 1}, 14);
    sky.gradient = Gradient::between(180, {.16f, .12f, .36f, 1}, {.02f, .14f, .22f, 1});
    Node &glow = scene.shape({area.x + area.w * .2f, area.y + area.h * .2f, area.w * .4f, area.h * .25f}, {0, 0, 0, 0},
                             area.h * .12f);
    glow.shadow = {0, 0, 60, 10, {.35f, .9f, .7f, .35f}};
    if (key == "off") {
        paint.glyphs.text(scene, {area.x, area.y + area.h / 2 - 20, area.w, 40}, "Style's own Home", kit::type::labelM,
                          faded(kWhite, .7f), Node::Align::Center);
        return;
    }
    const home::Bar design = home::barOf(key);
    const size_t count = std::min<size_t>(apps.size(), design == home::Bar::Halo ? 5 : 6);
    const bool vertical = design == home::Bar::Vertical;
    const float side = design == home::Bar::Cards || design == home::Bar::Halo ? 40 : vertical ? 24 : 30;
    const float gap = design == home::Bar::Horizon || design == home::Bar::Fade ? 16 : 8;
    const float content = static_cast<float>(count) * side + static_cast<float>(count ? count - 1 : 0) * gap;
    const Color accent = design == home::Bar::Neon ? Color{1, .31f, .85f, 1}
                         : design == home::Bar::Halo ? Color{.66f, .5f, 1, 1}
                                                    : Color{.66f, .78f, .98f, 1};
    float x = area.x + (area.w - content) / 2, y = area.y + area.h - side - 22;
    if (vertical) {
        x = area.x + area.w - side - 14;
        y = area.y + (area.h - content) / 2;
    }
    // The container, as the design has one.
    if (design == home::Bar::Aurora || design == home::Bar::Pills) {
        Node &dock = scene.shape({x - 10, y - 8, content + 20, side + 16}, faded(kWhite, .12f), (side + 16) / 2);
        dock.ring = {1, 0, faded(kWhite, .3f)};
    } else if (design == home::Bar::Horizon) {
        Node &band = scene.shape({area.x, y - 10, area.w, side + 20}, {0, 0, 0, 0});
        band.gradient = Gradient::between(180, {0, 0, 0, .05f}, {0, 0, 0, .55f});
    } else if (design == home::Bar::Neon) {
        Node &dock = scene.shape({x - 8, y - 7, content + 16, side + 14}, {.03f, .02f, .08f, .7f}, 6);
        dock.ring = {1, 0, faded(accent, .5f)};
    } else if (vertical) {
        Node &dock = scene.shape({x - 7, y - 7, side + 14, content + 14}, faded(kWhite, .12f), 10);
        dock.ring = {1, 0, faded(kWhite, .3f)};
    }
    if (design == home::Bar::Pills) {
        Node &tabs = scene.shape({area.x + area.w * .15f, y - 40, area.w * .7f, 22}, faded(kWhite, .12f), 11);
        tabs.ring = {1, 0, faded(kWhite, .3f)};
        scene.shape({area.x + area.w * .15f + 3, y - 37, area.w * .16f, 16}, faded(kWhite, .9f), 8);
    }
    const size_t focused = design == home::Bar::Halo ? count / 2 : 1;
    for (size_t i = 0; i < count; ++i) {
        Rect r = vertical ? Rect{x, y + static_cast<float>(i) * (side + gap), side, side}
                          : Rect{x + static_cast<float>(i) * (side + gap), y, side, side};
        float scale = i == focused ? 1.2f : 1;
        if (design == home::Bar::Halo) {
            const float distance = std::fabs(static_cast<float>(i) - static_cast<float>(focused));
            scale = std::max(.55f, 1 - .22f * distance) * (i == focused ? 1.3f : 1);
        }
        const float radius = design == home::Bar::Circles ? side / 2 * scale : 6 * scale;
        r = {r.x + r.w / 2 - r.w * scale / 2, r.y + r.h / 2 - r.h * scale / 2, r.w * scale, r.h * scale};
        if (i == focused) {
            Node &halo = scene.shape(r, {0, 0, 0, 0}, radius);
            halo.shadow = {0, 0, 16, 1, faded(accent, .7f)};
        }
        Node &base = scene.shape(r, kit::color::surface2, radius);
        base.backing = !apps[i].image.empty();
        if (!apps[i].image.empty())
            scene.picture(r, 0, radius).value = apps[i].image;
        if (i == focused && design != home::Bar::Horizon && design != home::Bar::Fade) {
            Node &ring = scene.shape(r, {0, 0, 0, 0}, radius);
            ring.ring = {0, 2, design == home::Bar::Neon || design == home::Bar::Cards ? accent : kWhite};
        }
        if (i == focused && (design == home::Bar::Horizon || design == home::Bar::Fade))
            scene.shape({r.x + r.w * .2f, r.y + r.h + 5, r.w * .6f, 2}, design == home::Bar::Fade ? kWhite : accent, 1);
    }
}

class AppBarScreen final : public ScreenView {
  public:
    std::string name() const override {
        return "app-bars";
    }
    void enter(ScreenHost &host) override {
        const std::string current = host.view().catalogue["layout"]["appBar"].string("off");
        for (size_t i = 0; i < kCount; ++i)
            if (current == kDesigns[i].key)
                selected_ = i;
        for (size_t i = 0; i < kCount; ++i)
            host.motion().set(id(i), i == selected_ ? 1 : 0);
        host.motion().set("ab:e", 1);
        host.motion().tween("ab:e", 0, kit::motion::ms::base, easing(kit::motion::ease::out), 60);
    }
    void leave(ScreenHost &host) override {
        host.motion().erasePrefix("ab:");
    }
    bool key(ScreenHost &host, Key key) override {
        if (key == Key::Ok) {
            apply(host);
            return true;
        }
        size_t next = selected_;
        if (key == Key::Left && selected_ % kColumns)
            --next;
        else if (key == Key::Right && selected_ % kColumns + 1 < kColumns && selected_ + 1 < kCount)
            ++next;
        else if (key == Key::Down && selected_ + kColumns < kCount)
            next += kColumns;
        else if (key == Key::Up) {
            if (selected_ < kColumns)
                return false; // the top bar
            next -= kColumns;
        }
        move(host, next);
        return key == Key::Left || key == Key::Right || key == Key::Down || key == Key::Up;
    }
    void focus(ScreenHost &host) override {
        move(host, selected_);
    }
    void click(ScreenHost &host, const std::string &hit) override {
        for (size_t i = 0; i < kCount; ++i)
            if (hit == id(i)) {
                move(host, i);
                apply(host);
            }
    }
    void build(ScreenHost &host, kit::Paint paint) override {
        Scene &scene = paint.scene;
        const float hidden = static_cast<float>(host.motion().value("ab:e"));
        scene.pushOpacity(1 - hidden);
        scene.pushTransform(Transform::translate(0, 24 * hidden));
        paint.glyphs.text(scene, {kit::canvas::safeX, 120, 1400, kit::type::headline.line},
                          msg("tv.bars.title", "App bar style"), kit::type::headline, kit::color::onSurface);
        paint.glyphs.text(scene, {kit::canvas::safeX, 120 + kit::type::headline.line, 1600, kit::type::body.line},
                          msg("tv.bars.hint", "OK applies a design to this profile's Home. Its options are in Appearance."),
                          kit::type::body, kit::color::onSurface2);
        const std::vector<Card> apps = appsRow(host.view().catalogue, host.view().guide, false).cards;
        const std::string current = host.view().catalogue["layout"]["appBar"].string("off");
        for (size_t i = 0; i < kCount; ++i) {
            const Rect area = cardRect(i);
            const float focus = host.barFocused() ? 0 : static_cast<float>(host.motion().value(id(i)));
            const float scale = 1 + .05f * focus;
            scene.pushTransform(Transform::scaleAbout(area, .5f, .5f, scale));
            if (focus > .01f) {
                Node &glow = scene.shape(area, {0, 0, 0, 0}, 14);
                glow.shadow = {0, 10, 36, 0, {0, 0, 0, .5f * focus}};
            }
            preview(paint, area, kDesigns[i].key, apps);
            Node &ring = scene.shape(area, {0, 0, 0, 0}, 14);
            ring.ring = {0, 3, faded(kWhite, focus)};
            if (current == kDesigns[i].key) {
                Node &mark = scene.shape({area.x + area.w - 40, area.y + 12, 28, 28}, kit::color::accent, 14);
                (void)mark;
                paint.glyphs.icon(scene, {area.x + area.w - 36, area.y + 16, 20, 20}, "check", {.05f, .05f, .08f, 1});
            }
            scene.hits.push_back({area, id(i)});
            scene.popTransform();
            paint.glyphs.text(scene, {area.x, area.y + area.h + 18, area.w, kit::type::labelL.line},
                              msg(std::string("tv.bars.") + kDesigns[i].key, kDesigns[i].title), kit::type::labelL,
                              kit::color::onSurface);
            paint.glyphs.text(scene, {area.x, area.y + area.h + 18 + kit::type::labelL.line, area.w,
                                      kit::type::labelS.line * 2},
                              msg(std::string("tv.bars.") + kDesigns[i].key + "Hint", kDesigns[i].blurb),
                              kit::type::labelS, kit::color::onSurface3, Node::Align::Left, 2);
        }
        scene.popTransform();
        scene.popOpacity();
    }

  private:
    static std::string id(size_t index) {
        return "ab:" + std::to_string(index);
    }
    void move(ScreenHost &host, size_t next) {
        if (next >= kCount)
            return;
        host.motion().tween(id(selected_), 0, kit::motion::ms::focus, easing(kit::motion::ease::out));
        selected_ = next;
        host.motion().tween(id(selected_), 1, kit::motion::ms::focus, easing(kit::motion::ease::out));
        host.invalidate();
    }
    void apply(ScreenHost &host) {
        const std::string value = kDesigns[selected_].key;
        Write write;
        write.operation = Operation::SetHomeLayout;
        write.body = [](const Json &target) {
            Json body = Json::object();
            body.set("appBar", target);
            return body;
        };
        write.readChannel = "catalogue";
        write.readOperation = Operation::HomeCatalogue;
        host.optimistic().set("layout.appBar", Json::text(value),
                              Json::text(host.view().catalogue["layout"]["appBar"].string("off")), write);
        host.open("home");
    }

    size_t selected_ = 0;
};
} // namespace

std::unique_ptr<ScreenView> makeAppBarScreen(const std::string &route) {
    return route == "app-bars" ? std::unique_ptr<ScreenView>(new AppBarScreen()) : nullptr;
}
} // namespace nativetv
