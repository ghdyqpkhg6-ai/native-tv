// Fork: Home's app bar in its nine designs (home_layout.hpp), drawn over the wallpaper or the live
// wallpaper, which never stops or reloads for it. Every design takes its places from barItem(), its
// focus from the same "f:" values Home's rows use (so focus fades out of one app as it fades into
// the next, at the bar's chosen speed), and its scroll from "rx". Nothing here needs a frame of its
// own: what moves is the focus, the scroll and the auto-hide ("bar:hide"), which the motion engine
// animates. There is no real-time blur: the renderer has no blur pass, and over a 4K video one
// would cost more than a TV should spend, so frosted glass is translucent layers, a light rim and a
// sheen, as the owner's request allowed.
#include "ui/application.hpp"
#include "ui/home_layout.hpp"
#include "ui/kit/ambience.hpp"
#include "ui/kit/widgets.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace nativetv {
namespace {
using home::Bar;
constexpr Color kClear = {0, 0, 0, 0};
constexpr Color kWhite = {1, 1, 1, 1};
constexpr Color kInk = {0, 0, 0, 1};

Color alpha(Color color, float amount) {
    color.a *= amount;
    return color;
}
float clamp01(float value) {
    return value < 0 ? 0 : value > 1 ? 1 : value;
}
Rect grown(Rect area, float by) {
    return {area.x - by, area.y - by, area.w + 2 * by, area.h + 2 * by};
}
std::string glyphOf(const Card &card) {
    return card.id == "lg-home" ? "home" : card.kind == "dev" ? "hdmi" : card.kind == "ch" ? "tv" : "apps";
}

struct Item {
    Rect rest;
    const Card *card;
    float focus, enter;
    size_t index;
};

// An app's icon, whole, rounded by `radius`, over a neutral placeholder that hides once the icon
// is on show.
void icon(kit::Paint paint, Rect area, const Card &card, float radius) {
    // A soft shadow under the tile, so it settles into the wallpaper rather than sitting on it.
    if (home::bar().fade) {
        Node &shadow = paint.scene.shape(area, kClear, radius);
        shadow.shadow = {0, area.h * .08f, area.h * .32f, 0, alpha(kInk, .55f)};
    }
    Node &base = paint.scene.shape(area, kit::color::surface2, radius);
    base.backing = !card.image.empty();
    if (!card.image.empty())
        paint.scene.picture(area, 0, radius).value = card.image;
    else
        paint.glyphs.icon(paint.scene, {area.x + area.w * .3f, area.y + area.h * .3f, area.w * .4f, area.h * .4f},
                          glyphOf(card), kWhite);
}
// The soft light around a focused item, in the accent, as strong as the bar's glow setting.
void glowAt(Scene &scene, Rect area, float radius, Color tint, float amount) {
    const float strength = amount * home::bar().glow;
    if (strength <= .01f)
        return;
    Node &glow = scene.shape(area, kClear, radius);
    glow.shadow = {0, 0, 30 + 26 * home::bar().glow, 2, alpha(tint, std::min(.75f, .5f * strength))};
}
void ringAt(Scene &scene, Rect area, float radius, Color tint, float width) {
    if (tint.a <= .01f)
        return;
    Node &ring = scene.shape(area, kClear, radius);
    ring.ring = {0, width, tint};
}
// Frosted glass: a translucent fill brighter at the top, a light rim and a soft shadow, as strong
// as the bar's background opacity.
void glass(Scene &scene, Rect area, float radius, float strength) {
    const float level = home::bar().opacity * 2 * strength; // 0 to 2 around the default .5
    if (level <= .01f)
        return;
    Node &panel = scene.shape(area, alpha(kWhite, .1f * level), radius);
    panel.gradient = Gradient::between(180, alpha(kWhite, .2f * level), alpha(kWhite, .06f * level));
    panel.ring = {1, 0, alpha(kWhite, std::min(.45f, .3f * level))};
    panel.shadow = {0, 14, 40, 0, alpha(kInk, std::min(.45f, .3f * level))};
}
// The focused app's name, in white at its focus.
void name(kit::Paint paint, const Item &item, float y, Node::Align align = Node::Align::Center,
          float x = -1) {
    if (!home::bar().names || item.focus <= .01f || item.card->title.empty())
        return;
    const float width = 440;
    const float left = x >= 0 ? (align == Node::Align::Right ? x - width : x) : item.rest.x + item.rest.w / 2 - width / 2;
    paint.glyphs.text(paint.scene, {left, y, width, kit::type::labelL.line}, item.card->title, kit::type::labelL,
                      alpha(kWhite, clamp01(item.focus)), align);
}
// Drawn with the item's focus scale about its centre, and its entrance.
struct Scaled {
    Scene &scene;
    bool scaled, faded;
    Scaled(Scene &into, Rect area, float scale, float opacity, float lift = 0)
        : scene(into), scaled(scale != 1 || lift != 0), faded(opacity < 1) {
        if (faded)
            scene.pushOpacity(opacity);
        if (scaled)
            scene.pushTransform(Transform::scaleAbout(area.x + area.w / 2, area.y + area.h / 2, scale, scale, 0, lift));
    }
    ~Scaled() {
        if (scaled)
            scene.popTransform();
        if (faded)
            scene.popOpacity();
    }
};

// The shade between the bar and the wallpaper: the bar's edge of the screen darkens softly, so
// icons and names read over any picture or video without a hard band.
void fadeShade(Scene &scene, const std::vector<Item> &items) {
    if (!home::bar().fade || items.empty())
        return;
    const float strength = .7f + .3f * home::bar().opacity;
    if (home::barVertical()) {
        const float x = items.front().rest.x;
        if (home::bar().alternate) {
            Node &shade = scene.shape({0, 0, x + items.front().rest.w + 460, kit::canvas::height}, kClear);
            shade.gradient = Gradient::between(270, alpha(kInk, 0), alpha(kInk, strength));
        } else {
            Node &shade = scene.shape({x - 460, 0, kit::canvas::width - x + 460, kit::canvas::height}, kClear);
            shade.gradient = Gradient::between(90, alpha(kInk, 0), alpha(kInk, strength));
        }
        return;
    }
    const Rect first = items.front().rest;
    if (home::bar().alternate) {
        const float bottom = first.y + first.h + 400;
        Node &shade = scene.shape({0, 0, kit::canvas::width, bottom}, kClear);
        shade.gradient = Gradient::between(0, alpha(kInk, 0), alpha(kInk, strength));
    } else {
        // Clear at the top, a gentle rise over the videos, and deepest behind the tiles, so the bar
        // fades into the wallpaper with no edge to it.
        const float top = first.y - (home::bar().design == Bar::Pills ? 620 : 480);
        const float height = kit::canvas::height - top;
        Node &shade = scene.shape({0, top, kit::canvas::width, height}, kClear);
        shade.gradient.angle = 180;
        shade.gradient.stops = 3;
        shade.gradient.color[0] = alpha(kInk, 0);
        shade.gradient.at[0] = 0;
        shade.gradient.color[1] = alpha(kInk, strength * .55f);
        shade.gradient.at[1] = std::min(.9f, std::max(.3f, (first.y - 40 - top) / height));
        shade.gradient.color[2] = alpha(kInk, strength);
        shade.gradient.at[2] = 1;
    }
}

// The Glass Pill Tabs' categories: a frosted pill with a light pill under the selected one, which
// slides between categories as focus moves along them.
void pillTabs(kit::Paint paint, const Frame &frame, size_t rowIndex, float appsTop) {
    const HomeState &home = frame.state.home;
    const CardRow &row = home.rows[rowIndex];
    if (row.cards.empty())
        return;
    Scene &scene = paint.scene;
    const kit::TypeStyle &style = kit::type::labelL;
    std::vector<float> widths;
    float total = 0;
    for (const Card &card : row.cards) {
        widths.push_back(paint.glyphs.measure(card.title, style) + 64);
        total += widths.back();
    }
    const float height = 68, pad = 8;
    const Rect barArea = {kit::canvas::width / 2 - total / 2 - pad, appsTop - height - 48, total + 2 * pad, height};
    glass(scene, barArea, height / 2, 1);
    glowAt(scene, barArea, height / 2, home::bar().accent, .35f);
    // Where the light pill stands: between the focused categories while focus moves along them,
    // on the selected one otherwise.
    std::vector<float> lefts;
    float x = barArea.x + pad;
    for (float width : widths) {
        lefts.push_back(x);
        x += width;
    }
    float weight = 0, left = 0, width = 0;
    if (home.row == rowIndex && !home.navigation)
        for (size_t i = 0; i < row.cards.size(); ++i) {
            const float focus = clamp01(static_cast<float>(frame.motion.value(home::id("f", rowIndex, i))));
            weight += focus;
            left += focus * lefts[i];
            width += focus * widths[i];
        }
    if (weight < .05f) {
        const size_t selected = std::min(home.pillTab, row.cards.size() - 1);
        left = lefts[selected];
        width = widths[selected];
    } else {
        left /= weight;
        width /= weight;
    }
    Node &pill = scene.shape({left, barArea.y + pad, width, height - 2 * pad}, alpha(kWhite, .9f), (height - 2 * pad) / 2);
    pill.shadow = {0, 4, 18, 0, alpha(kInk, .3f)};
    for (size_t i = 0; i < row.cards.size(); ++i) {
        const Rect area = {lefts[i], barArea.y, widths[i], height};
        const bool under = left < lefts[i] + widths[i] / 2 && left + width > lefts[i] + widths[i] / 2;
        paint.glyphs.text(scene, {area.x, area.y + (height - style.line) / 2, area.w, style.line}, row.cards[i].title,
                          style, under ? Color{.08f, .08f, .1f, 1} : alpha(kWhite, .86f), Node::Align::Center);
        scene.hits.push_back({area, home::hit(rowIndex, i)});
    }
}
} // namespace

void drawAppBar(kit::Paint paint, const Frame &frame) {
    const HomeState &home = frame.state.home;
    const Motion &motion = frame.motion;
    Scene &scene = paint.scene;
    const home::BarConfig &config = home::bar();
    size_t appsRow = home.rows.size(), tabsRow = home.rows.size();
    for (size_t i = 0; i < home.rows.size(); ++i) {
        if (home::isApps(home.rows[i]))
            appsRow = i;
        else if (home.rows[i].key == "tabs")
            tabsRow = i;
    }
    if (appsRow >= home.rows.size())
        return;
    const float shown = 1 - clamp01(static_cast<float>(motion.value("bar:hide")));
    if (shown <= .002f)
        return;
    scene.pushOpacity(shown);
    const CardRow &row = home.rows[appsRow];
    const float scroll = static_cast<float>(motion.value(home::id("rx", appsRow)));
    const float leaving = static_cast<float>(motion.value("pgx"));
    std::vector<Item> items;
    for (size_t ci = 0; ci < row.cards.size(); ++ci) {
        Rect rest = home::barItem(row, ci);
        if (home::barVertical())
            rest.y += scroll;
        else
            rest.x += scroll;
        Item item;
        item.rest = rest;
        item.card = &row.cards[ci];
        item.focus = clamp01(static_cast<float>(motion.value(home::id("f", appsRow, ci))));
        item.enter = clamp01(1 - std::max(static_cast<float>(motion.value(home::id("EC", appsRow, ci))), leaving));
        if (row.cards[ci].hidden) // listed only while the apps are edited
            item.enter *= .4f;
        item.index = ci;
        items.push_back(item);
    }
    fadeShade(scene, items);
    const float side = home::barIcon();
    // The bar's container, across the items on screen.
    float minX = kit::canvas::width, maxX = 0, minY = kit::canvas::height, maxY = 0;
    for (const Item &item : items) {
        if (item.rest.x + item.rest.w < -40 || item.rest.x > kit::canvas::width + 40 || item.rest.y + item.rest.h < -40 ||
            item.rest.y > kit::canvas::height + 40)
            continue;
        minX = std::min(minX, item.rest.x);
        maxX = std::max(maxX, item.rest.x + item.rest.w);
        minY = std::min(minY, item.rest.y);
        maxY = std::max(maxY, item.rest.y + item.rest.h);
    }
    const bool any = maxX > minX;
    const double seconds = frame.nowMs / 1000.0;
    const bool still = kit::motionLevel() == kit::MotionLevel::Off;
    switch (config.design) {
    case Bar::Aurora:
        if (any)
            glass(scene, {std::max(24.0f, minX - 26), minY - 22, std::min(kit::canvas::width - 48, maxX - minX + 52), side + 44},
                  (side + 44) / 2, 1);
        break;
    case Bar::Horizon: {
        const float level = config.opacity * 2;
        Node &band = scene.shape({0, minY - 32, kit::canvas::width, side + 64}, kClear);
        band.gradient = Gradient::between(180, alpha(kInk, .1f * level), alpha(kInk, .5f * level));
        break;
    }
    case Bar::Neon:
        if (any) {
            Node &panel = scene.shape({std::max(24.0f, minX - 20), minY - 18, std::min(kit::canvas::width - 48, maxX - minX + 40), side + 36},
                                      {.03f, .02f, .08f, std::min(1.0f, 1.2f * config.opacity)}, 18);
            panel.ring = {1, 0, alpha(config.accent, .3f)};
            panel.shadow = {0, 0, 40, 0, alpha(config.accent, .18f * config.glow)};
        }
        break;
    case Bar::Vertical:
        if (any)
            glass(scene, {minX - 16, std::max(130.0f, minY - 16), side + 32, std::min(kit::canvas::height - 160, maxY - minY + 32)}, 26, 1);
        break;
    default:
        break;
    }
    // The items. The carousel draws the far ones first, so the focused one stands in front.
    std::vector<const Item *> order;
    for (const Item &item : items)
        order.push_back(&item);
    const float pitch = side + home::barGap();
    const float middle = pitch > 0 ? -scroll / pitch : 0; // the carousel's focus, between items while it slides
    if (config.design == Bar::Halo)
        std::sort(order.begin(), order.end(), [middle](const Item *a, const Item *b) {
            return std::fabs(static_cast<float>(a->index) - middle) > std::fabs(static_cast<float>(b->index) - middle);
        });
    float weight = 0, centre = 0; // the focus's centre, for the moving underline
    for (const Item *pointer : order) {
        const Item &item = *pointer;
        const Rect r = item.rest;
        if (r.x + r.w < -260 || r.x > kit::canvas::width + 260 || r.y + r.h < -260 || r.y > kit::canvas::height + 260)
            continue;
        const float f = item.focus;
        const float dim = .62f + .38f * f; // apps rest a little faded; focus fades one in as it leaves another
        weight += f;
        centre += f * (r.x + r.w / 2);
        switch (config.design) {
        case Bar::Aurora: {
            Scaled group(scene, r, 1 + .16f * f, item.enter * dim);
            glowAt(scene, r, side * .24f, config.accent, f);
            icon(paint, r, *item.card, side * .24f);
            ringAt(scene, r, side * .24f, alpha(kWhite, .9f * f), 3);
            break;
        }
        case Bar::Horizon:
        case Bar::Fade: {
            Scaled group(scene, r, 1 + .12f * f, item.enter * dim);
            glowAt(scene, r, side * .22f, config.accent, f * .6f);
            icon(paint, r, *item.card, side * .22f);
            break;
        }
        case Bar::Cards: {
            Scaled group(scene, r, 1 + .3f * f, item.enter * dim, -16 * f);
            glowAt(scene, r, 20, config.accent, f);
            icon(paint, r, *item.card, 20);
            ringAt(scene, r, 20, alpha(config.accent, f), 2.5f);
            // A soft reflection of the focus under the card.
            Node &reflection = scene.shape({r.x + 10, r.y + r.h + 8, r.w - 20, 30}, kClear, 12);
            reflection.gradient = Gradient::between(180, alpha(config.accent, .28f * f * std::min(1.0f, config.glow + .2f)),
                                                    alpha(config.accent, 0));
            break;
        }
        case Bar::Neon: {
            // The edge lights breathe slowly unless the owner switched animations off.
            const float pulse = still ? .5f : .5f + .5f * static_cast<float>(std::sin(seconds * 2.2 + item.index * .7));
            Scaled group(scene, r, 1 + .12f * f, item.enter * dim);
            glowAt(scene, r, 12, config.accent, f * 1.3f);
            icon(paint, r, *item.card, 12);
            ringAt(scene, r, 12, alpha(config.accent, .22f + .14f * pulse + .64f * f), 1.5f + 1.5f * f);
            break;
        }
        case Bar::Circles: {
            Scaled group(scene, r, 1 + .14f * f, item.enter * dim);
            glowAt(scene, r, side / 2, config.accent, f);
            icon(paint, r, *item.card, side / 2);
            ringAt(scene, r, side / 2, alpha(kWhite, .22f), 1);
            ringAt(scene, grown(r, 9), side / 2 + 9, alpha(config.accent, f), 2.5f);
            break;
        }
        case Bar::Vertical: {
            Scaled group(scene, r, 1 + .18f * f, item.enter * dim);
            glowAt(scene, r, side * .24f, config.accent, f);
            icon(paint, r, *item.card, side * .24f);
            ringAt(scene, r, side * .24f, alpha(kWhite, .85f * f), 2.5f);
            break;
        }
        case Bar::Halo: {
            // Smaller and fainter with distance from the focus, so the carousel narrows away from it.
            const float distance = std::fabs(static_cast<float>(item.index) - middle);
            const float scale = std::max(.62f, 1 - .12f * distance) * (1 + .3f * f);
            const float opacity = std::max(.3f, 1 - .17f * distance);
            Scaled group(scene, r, scale, item.enter * opacity);
            glowAt(scene, r, 26, config.accent, f * 1.2f);
            icon(paint, r, *item.card, 26);
            ringAt(scene, r, 26, alpha(kit::focus::mix(kWhite, config.accent, .5f), f), 3);
            break;
        }
        case Bar::Pills: {
            Scaled group(scene, r, 1 + .14f * f, item.enter * dim);
            glowAt(scene, r, side * .22f, config.accent, f);
            icon(paint, r, *item.card, side * .22f);
            ringAt(scene, r, side * .22f, alpha(kWhite, .85f * f), 2.5f);
            break;
        }
        default:
            break;
        }
        scene.hits.push_back({r, home::hit(appsRow, item.index)});
    }
    // The moving underline (Horizon, Minimal Fade): at the focus, sliding between apps as it moves.
    if ((config.design == Bar::Horizon || config.design == Bar::Fade) && weight > .02f && any) {
        const float x = centre / weight, width = side * (config.design == Bar::Fade ? .5f : .72f);
        const float y = minY + side + (config.design == Bar::Fade ? 14 : 18);
        Node &line = scene.shape({x - width / 2, y, width, config.design == Bar::Fade ? 3.0f : 4.0f},
                                 alpha(config.design == Bar::Fade ? kWhite : config.accent, std::min(1.0f, weight)), 2);
        line.shadow = {0, 0, 14, 1, alpha(config.accent, .6f * config.glow * std::min(1.0f, weight))};
    }
    // The carousel's place among the apps.
    if (config.design == Bar::Halo && row.cards.size() > 1 && row.cards.size() <= 12 && any) {
        const float count = static_cast<float>(row.cards.size()), dot = 9, gap = 14;
        const float left = kit::canvas::width / 2 - (count * dot + (count - 1) * gap) / 2;
        for (size_t i = 0; i < row.cards.size(); ++i) {
            const float on = clamp01(1 - std::fabs(static_cast<float>(i) - middle));
            scene.shape({left + static_cast<float>(i) * (dot + gap), minY + side * 1.3f + 34, dot, dot},
                        alpha(kWhite, .3f + .65f * on), dot / 2);
        }
    }
    // The focused app's name.
    for (const Item &item : items) {
        if (item.focus <= .01f)
            continue;
        const Rect r = item.rest;
        switch (config.design) {
        case Bar::Vertical:
            if (config.alternate)
                name(paint, item, r.y + r.h / 2 - kit::type::labelL.line / 2, Node::Align::Left, r.x + r.w + 34);
            else
                name(paint, item, r.y + r.h / 2 - kit::type::labelL.line / 2, Node::Align::Right, r.x - 34);
            break;
        case Bar::Horizon:
        case Bar::Fade:
            name(paint, item, config.alternate ? r.y + r.h + 30 : r.y - kit::type::labelL.line - 22);
            break;
        case Bar::Halo:
            name(paint, item, r.y + r.h * 1.3f + 52);
            break;
        case Bar::Cards:
            name(paint, item, r.y + r.h * 1.15f + 48);
            break;
        default:
            name(paint, item, r.y + r.h + (config.design == Bar::Aurora || config.design == Bar::Neon ? 34 : 26));
            break;
        }
    }
    if (tabsRow < home.rows.size())
        pillTabs(paint, frame, tabsRow, home::barTop());
    scene.popOpacity();
}
} // namespace nativetv
