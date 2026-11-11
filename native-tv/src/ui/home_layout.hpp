#pragma once
#include "domain/models.hpp"
#include "ui/kit/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

// What Home's controller and view agree on: how wide a row's items are and the ids of the
// animated values. Ids stay under 16 characters so building one allocates nothing.
namespace nativetv {
namespace home {
constexpr size_t kGridColumns = static_cast<size_t>(kit::layout::tileCols);
constexpr float kGridPitch = kit::layout::tilePitchY;
constexpr size_t kSpotlightMax = 6;

// Hue in degrees, saturation and lightness in 0..1.
inline Color hsl(float hue, float saturation, float lightness) {
    float chroma = (1 - std::fabs(2 * lightness - 1)) * saturation, h = hue / 60;
    float x = chroma * (1 - std::fabs(std::fmod(h, 2.0f) - 1)), m = lightness - chroma / 2;
    float r = 0, g = 0, b = 0;
    if (h < 1) {
        r = chroma;
        g = x;
    } else if (h < 2) {
        r = x;
        g = chroma;
    } else if (h < 3) {
        g = chroma;
        b = x;
    } else if (h < 4) {
        g = x;
        b = chroma;
    } else if (h < 5) {
        r = x;
        b = chroma;
    } else {
        r = chroma;
        b = x;
    }
    return {r + m, g + m, b + m, 1};
}
inline unsigned hashOf(const std::string &text) {
    unsigned hash = 2166136261u;
    for (unsigned char c : text)
        hash = (hash ^ c) * 16777619u;
    return hash;
}
inline bool isApps(const CardRow &row) {
    return row.key == "app";
}

// Fork: Home's style, chosen in Home settings > Appearance ("style" in the Home layout). Google is
// v0.118.0's look; Classic and Liquid Glass draw Home's apps as landscape tiles on a tray, and
// Ribbon as one strip of tiles along the bottom over the wallpaper, Home's only row. The Apps page
// keeps its circles in every style. Set once per frame by the application, before Home's layout
// and motion read it.
enum class Style { Google, Classic, Glass, Ribbon };
inline Style &styleSlot() {
    static Style current = Style::Google;
    return current;
}
inline Style style() {
    return styleSlot();
}
inline void setStyle(Style value) {
    styleSlot() = value;
}
inline Style styleOf(const std::string &name) {
    return name == "classic" ? Style::Classic
           : name == "glass" ? Style::Glass
           : name == "ribbon" ? Style::Ribbon
                              : Style::Google;
}
// Fork: the active profile's live wallpaper ("liveWallpaper" in the Home layout) as the catalogue
// lists it in "videos" ({id, title, credit}). A USB drive's video whose drive is unplugged gives
// way to the first of the TV's own videos until the drive is back (service/home_videos.hpp, which
// the server's choice matches); invalid Json when there is none of either: the still wallpaper.
inline Json liveWallpaper(const Json &catalogue) {
    const std::string id = catalogue["layout"]["liveWallpaper"].string();
    if (id.empty())
        return Json();
    for (const Json &video : catalogue["videos"].items())
        if (video["id"].string() == id)
            return video;
    if (id.compare(0, 4, "usb/") == 0)
        for (const Json &video : catalogue["videos"].items())
            if (video["id"].string().compare(0, 4, "usb/") != 0)
                return video;
    return Json();
}
// Fork: Home's app bar (Home settings > Appearance > App bar style, "appBar" in the Home layout):
// nine layouts of Home's apps over the wallpaper or live wallpaper, after the owner's reference
// sheet. With one chosen, Home's page is the bar alone (the Glass Pill Tabs add a row of
// categories above it); the Apps page keeps its circles. Set once per frame by the application
// from the layout, with its options, before Home's layout, motion and drawing read it.
enum class Bar { Off, Aurora, Horizon, Cards, Neon, Circles, Vertical, Halo, Fade, Pills };
struct BarConfig {
    Bar design = Bar::Off;
    float size = 1;         // "barSize": .8, 1 or 1.25
    float spacing = 1;      // "barSpacing": .5, 1 or 1.6 times the design's own gap
    float opacity = .5f;    // "barOpacity": the container's strength, 0 to 1
    float glow = 1;         // "barGlow": 0, .5, 1 or 1.6
    Color accent = {.66f, .78f, .98f, 1}; // "barAccent"
    bool names = true;      // "barNames": the focused app's name
    bool fade = true;       // "barFade": a soft shade from the bar into the wallpaper
    bool alternate = false; // "barPosition": top instead of bottom, left instead of right
    int focusMs = 260;      // "barSpeed": 0 (instant), 110, 260 or 520
    int hideMs = 0;         // "barHide": 0 (never) or the idle time before the bar fades away
    bool operator==(const BarConfig &other) const {
        return design == other.design && size == other.size && spacing == other.spacing &&
               opacity == other.opacity && glow == other.glow && accent.r == other.accent.r &&
               accent.g == other.accent.g && accent.b == other.accent.b && names == other.names &&
               fade == other.fade && alternate == other.alternate && focusMs == other.focusMs &&
               hideMs == other.hideMs;
    }
    bool operator!=(const BarConfig &other) const {
        return !(*this == other);
    }
};
inline BarConfig &barSlot() {
    static BarConfig current;
    return current;
}
inline const BarConfig &bar() {
    return barSlot();
}
inline void setBar(const BarConfig &value) {
    barSlot() = value;
}
inline bool barMode() {
    return bar().design != Bar::Off;
}
inline Bar barOf(const std::string &name) {
    static const char *const names[] = {"off",     "aurora",   "horizon", "cards", "neon",
                                        "circles", "vertical", "halo",    "fade",  "pills"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (name == names[i])
            return static_cast<Bar>(i);
    return Bar::Off;
}
inline Color barAccentOf(const std::string &name) {
    if (name == "magenta")
        return {1, .31f, .85f, 1};
    if (name == "cyan")
        return {.24f, .9f, 1, 1};
    if (name == "amber")
        return {1, .7f, .28f, 1};
    if (name == "green")
        return {.49f, 1, .54f, 1};
    if (name == "red")
        return {1, .36f, .36f, 1};
    if (name == "white")
        return {1, 1, 1, 1};
    if (name == "violet")
        return {.66f, .5f, 1, 1};
    return {.66f, .78f, .98f, 1}; // "blue", the kit's accent
}
// Each design's own defaults stand where the owner kept "auto": the Neon Edge glows magenta, the
// Halo Carousel violet, and the Minimal Fade Bar hides after eight seconds.
inline BarConfig barConfigOf(const Json &layout) {
    BarConfig config;
    config.design = barOf(layout["appBar"].string());
    const std::string size = layout["barSize"].string("m");
    config.size = size == "s" ? .8f : size == "l" ? 1.25f : 1;
    const std::string spacing = layout["barSpacing"].string("normal");
    config.spacing = spacing == "tight" ? .5f : spacing == "wide" ? 1.6f : 1;
    config.opacity = static_cast<float>(std::atoi(layout["barOpacity"].string("50").c_str())) / 100;
    const std::string glow = layout["barGlow"].string("medium");
    config.glow = glow == "off" ? 0 : glow == "low" ? .5f : glow == "high" ? 1.6f : 1;
    std::string accent = layout["barAccent"].string("auto");
    if (accent == "auto")
        accent = config.design == Bar::Neon ? "magenta" : config.design == Bar::Halo ? "violet" : "blue";
    config.accent = barAccentOf(accent);
    config.names = layout["barNames"].boolean(true);
    config.fade = layout["barFade"].boolean(true);
    config.alternate = layout["barPosition"].string("default") == "alternate";
    const std::string speed = layout["barSpeed"].string("smooth");
    config.focusMs = speed == "instant" ? 0 : speed == "fast" ? 110 : speed == "cinematic" ? 520 : 260;
    const std::string hide = layout["barHide"].string("auto");
    config.hideMs = hide == "auto" ? (config.design == Bar::Fade ? 8000 : 0) : std::atoi(hide.c_str()) * 1000;
    return config;
}
// Home's row of apps drawn as the app bar (never the Apps page's circles, `named`).
inline bool barRow(const CardRow &row, bool named = false) {
    return isApps(row) && !named && barMode();
}
inline bool barVertical() {
    return bar().design == Bar::Vertical;
}
// The bar's icon side and gap at the chosen size and spacing.
inline float barIcon() {
    float base = 104;
    switch (bar().design) {
    case Bar::Horizon:
        base = 84;
        break;
    case Bar::Cards:
    case Bar::Halo:
        base = 150;
        break;
    case Bar::Neon:
        base = 116;
        break;
    case Bar::Vertical:
        base = 84;
        break;
    case Bar::Fade:
        base = 72;
        break;
    default:
        break;
    }
    return std::round(base * 1.25f * bar().size); // a quarter larger than the reference's proportions: read from the sofa
}
inline float barGap() {
    float base = 22;
    switch (bar().design) {
    case Bar::Horizon:
    case Bar::Fade:
        base = 60;
        break;
    case Bar::Cards:
    case Bar::Circles:
        base = 30;
        break;
    case Bar::Neon:
        base = 14;
        break;
    case Bar::Vertical:
        base = 18;
        break;
    case Bar::Halo:
        base = 44;
        break;
    default:
        break;
    }
    return std::round(base * bar().spacing);
}
// Where a horizontal bar's icons stand: clear of the screen's foot, or under the top bar.
inline float barTop() {
    if (bar().alternate)
        return bar().design == Bar::Pills ? 250 : 176;
    float margin = 96;
    switch (bar().design) {
    case Bar::Horizon:
        margin = 52;
        break;
    case Bar::Cards:
        margin = 130;
        break;
    case Bar::Halo:
        margin = 170;
        break;
    case Bar::Fade:
        margin = 76;
        break;
    default:
        break;
    }
    return kit::canvas::height - margin - barIcon();
}
// The bar's item `column`, before the row's scroll ("rx", along the bar's own axis).
inline Rect barItem(const CardRow &row, size_t column) {
    const float side = barIcon(), pitch = side + barGap();
    const float count = static_cast<float>(row.cards.size());
    if (barVertical()) {
        const float content = count * pitch - barGap();
        const float top = std::max(176.0f, (kit::canvas::height - content) / 2);
        const float x = bar().alternate ? 64 : kit::canvas::width - 64 - side;
        return {x, top + static_cast<float>(column) * pitch, side, side};
    }
    float left;
    if (bar().design == Bar::Halo) {
        left = kit::canvas::width / 2 - side / 2; // the focus is brought to the middle
    } else {
        const float content = count * pitch - barGap();
        const float room = kit::canvas::width - 2 * kit::canvas::safeX;
        left = content <= room ? (kit::canvas::width - content) / 2 : kit::canvas::safeX;
    }
    return {left + static_cast<float>(column) * pitch, barTop(), side, side};
}
// The bar's scroll target with focus on `column`: the carousel keeps the focus in the middle, the
// stack scrolls when it is taller than the screen, and a bar wider than the screen follows the
// focus as Home's rows do.
inline float barScrollTarget(const CardRow &row, size_t column) {
    const float side = barIcon(), pitch = side + barGap();
    const size_t count = row.cards.size();
    if (bar().design == Bar::Halo)
        return -static_cast<float>(column) * pitch;
    const float content = static_cast<float>(count) * pitch - barGap();
    if (barVertical()) {
        const float room = std::max(0.0f, content - (kit::canvas::height - 236));
        const float keep = static_cast<float>(std::min<size_t>(column, 3));
        return -std::min(std::max(0.0f, (static_cast<float>(column) - keep) * pitch), room);
    }
    if (content <= kit::canvas::width - 2 * kit::canvas::safeX)
        return 0;
    return kit::geometry::rowScrollTarget(static_cast<int>(column), static_cast<int>(count), side, barGap());
}
// Home's row of apps drawn as tiles: every style but Google's, never the Apps page (`named`), and
// never under an app bar.
inline bool tiles(const CardRow &row, bool named = false) {
    return isApps(row) && !named && style() != Style::Google && !barMode();
}
// The Ribbon strip: tiles edge to edge on a band that stands clear of the screen's foot.
constexpr float kRibbonTop = 800, kRibbonTileW = 200, kRibbonTileH = 200;

// Fork: Home as a strip of apps (the Ribbon, or an app bar) keeps one row of videos besides them:
// the owner's YouTube subscriptions, or YouTube's trending ones without an account.
inline bool strip() {
    return style() == Style::Ribbon || barMode();
}
inline bool videoRow(const CardRow &row) {
    return row.key == "subs" || row.key == "youtube";
}
// The video row's name, standing clear above the bar (and the Glass Pill Tabs' categories, and a
// focused app's name over it), or above the Ribbon's strip. A bar at the top or down the side
// leaves the screen's foot to the videos.
inline float videoRowLabelY() {
    constexpr float kRowHeight = kit::layout::rowLabelH + kit::layout::rowLabelGap + kit::layout::cardH;
    float above = kRibbonTop;
    if (barMode()) {
        if (barVertical() || bar().alternate)
            return kit::canvas::height - 96 - kRowHeight;
        above = barTop() - (bar().design == Bar::Pills                                ? 116
                            : bar().design == Bar::Horizon || bar().design == Bar::Fade ? 72
                            : bar().design == Bar::Cards || bar().design == Bar::Halo   ? 60
                                                                                        : 24);
    }
    return above - 28 - kRowHeight;
}
// How far the video row is drawn from where Home's row `rowIndex` would stand.
inline float videoRowShift(size_t rowIndex) {
    return videoRowLabelY() - kit::geometry::rowLabelY(static_cast<int>(rowIndex));
}

inline float itemWidth(const CardRow &row, bool named = false) {
    if (barRow(row, named))
        return barIcon();
    if (tiles(row, named))
        return style() == Style::Ribbon ? kRibbonTileW : kit::layout::cardH; // square: the icon whole
    return isApps(row) ? kit::layout::appD : kit::layout::cardW;
}
// `named`: a row of apps that carries every name (the Apps page), so the circles stand wider.
inline float itemGap(const CardRow &row, bool named = false) {
    if (barRow(row, named))
        return barGap();
    if (tiles(row, named))
        return style() == Style::Ribbon ? 0 : kit::layout::cardGap;
    return named && isApps(row) ? kit::layout::appGapNamed : kit::layout::cardGap;
}
inline std::string id(const char *name, size_t row, size_t column) {
    return std::string(name) + ":" + std::to_string(row) + "." + std::to_string(column);
}
inline std::string id(const char *name, size_t index) {
    return std::string(name) + ":" + std::to_string(index);
}
// The pointer's id for an item: "card:<row>:<column>".
inline std::string hit(size_t row, size_t column) {
    return "card:" + std::to_string(row) + ":" + std::to_string(column);
}
// Row n's items on the shared canvas, before the row's own scroll and the stack's offset.
inline Rect item(const CardRow &row, size_t rowIndex, size_t column, bool named = false) {
    if (barRow(row, named))
        return barItem(row, column);
    float width = itemWidth(row, named), gap = itemGap(row, named);
    float x = kit::canvas::safeX + static_cast<float>(column) * (width + gap);
    if (tiles(row, named) && style() == Style::Ribbon)
        return {x, kRibbonTop, width, kRibbonTileH};
    if (videoRow(row) && strip())
        return {x, videoRowLabelY() + kit::layout::rowLabelH + kit::layout::rowLabelGap, width, kit::layout::cardH};
    return {x, kit::geometry::rowItemsY(static_cast<int>(rowIndex)), width,
            isApps(row) && !tiles(row, named) ? kit::layout::appD : kit::layout::cardH};
}
// The apps grid's scroll target while focus is on `column`: three rows fit under the page
// title, so the grid follows once focus passes the second row, and stops when the last row is
// in view.
inline float gridScrollTarget(size_t column, size_t count) {
    size_t rows = (count + kGridColumns - 1) / kGridColumns, at = column / kGridColumns;
    float visible = 3;
    float room = rows > visible ? (static_cast<float>(rows) - visible) * kGridPitch : 0;
    float wanted = at > 1 ? static_cast<float>(at - 1) * kGridPitch : 0;
    return -(wanted > room ? room : wanted);
}
// The Apps page by kind starts at layout::appsTop and holds this many rows whole.
constexpr size_t kKindRowsVisible =
    static_cast<size_t>((kit::canvas::height - kit::layout::appsTop) / kit::layout::rowPitch);
// Its scroll target while focus is on kind row `row` of `rows`: the rows keep their place until
// focus passes the second, then the page follows so the next row stays in view, and the last rows
// end flush with the screen.
inline float kindStackTarget(size_t row, size_t rows) {
    size_t last = rows > kKindRowsVisible ? rows - kKindRowsVisible : 0;
    size_t first = row > 1 ? row - 1 : 0;
    return -kit::layout::rowPitch * static_cast<float>(std::min(first, last));
}
} // namespace home
} // namespace nativetv
