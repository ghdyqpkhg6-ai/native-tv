#include "ui/kit/ambience.hpp"
#include <algorithm>
#include <cmath>

namespace nativetv {
namespace kit {
namespace {
constexpr double kTau = 6.283185307179586;
constexpr Color kClear = {0, 0, 0, 0};
MotionLevel level_ = MotionLevel::Subtle;

// -1..1 with a period in seconds; phase is a fraction of a period.
float wave(double seconds, double period, double phase) {
    return static_cast<float>(std::sin((seconds / period + phase) * kTau));
}
Color aura(unsigned value) {
    return rgb(value);
}
// A soft horizontal band: clear at both edges, `color` at `alpha` through the middle, tilted by
// `angle` (CSS degrees, 180 is top to bottom).
void band(Scene &scene, Rect area, float angle, Color color, float alpha) {
    if (alpha <= .002f)
        return;
    Node &node = scene.shape(area, kClear);
    node.gradient.stops = 3;
    node.gradient.angle = angle;
    node.gradient.color[0] = focus::withAlpha(color, 0);
    node.gradient.at[0] = 0;
    node.gradient.color[1] = focus::withAlpha(color, alpha);
    node.gradient.at[1] = .5f;
    node.gradient.color[2] = focus::withAlpha(color, 0);
    node.gradient.at[2] = 1;
}
struct Band {
    float y, h, angle, swing, period, alpha;
    unsigned from, to;
    float phase;
};
// Periods are long and unrelated, so the pattern never visibly repeats; alphas stay low so text
// and cards keep their contrast. The third band is Full's.
const Band kBands[] = {{140, 640, 166, 70, 47, .19f, 0x2BD4BD, 0x7C6CF0, 0},
                       {470, 700, 14, 90, 61, .15f, 0x3D7BF2, 0x45E0A0, .37f},
                       {-80, 560, 152, 60, 73, .12f, 0x8E6CF0, 0xE06BB0, .71f}};
} // namespace

MotionLevel motionLevelOf(const std::string &value) {
    if (value == "off")
        return MotionLevel::Off;
    if (value == "full")
        return MotionLevel::Full;
    return MotionLevel::Subtle;
}
void setMotionLevel(MotionLevel level) {
    level_ = level;
}
MotionLevel motionLevel() {
    return level_;
}

float zoomTarget(float target) {
    return level_ == MotionLevel::Full ? 1 + (target - 1) * 1.25f : target;
}

void focusGlow(Scene &scene, Rect area, float rounding, Color tint, float focus) {
    if (level_ == MotionLevel::Off)
        return;
    const float amount = focus::clamp01(focus);
    if (amount <= .004f)
        return;
    const bool full = level_ == MotionLevel::Full;
    Node &glow = scene.shape(area, kClear, rounding);
    glow.shadow = {0, 0, full ? 34.0f : 24.0f, 2,
                   focus::withAlpha(focus::mix(tint, color::onSurface, .35f),
                                    (full ? .55f : .38f) * amount)};
}

void aurora(Paint paint, double seconds, float amount, Color tint, float stackY) {
    if (level_ == MotionLevel::Off || amount <= .002f)
        return;
    const bool full = level_ == MotionLevel::Full;
    const float strength = (full ? 1.0f : .72f) * focus::clamp01(amount);
    const float depth = std::max(-40.0f, std::min(40.0f, stackY * .06f));
    const int count = full ? 3 : 2;
    for (int i = 0; i < count; ++i) {
        const Band &b = kBands[i];
        // Subtle moves at two thirds of Full's pace.
        const double t = full ? seconds : seconds * .66;
        const float y = b.y + b.swing * wave(t, b.period, b.phase) + depth * (i + 1) / count;
        const float angle = b.angle + 9 * wave(t, b.period * 1.7, b.phase + .25);
        const float hue = .5f + .5f * wave(t, b.period * 2.3, b.phase + .5);
        Color color = focus::mix(aura(b.from), aura(b.to), hue);
        color = focus::mix(color, tint, .22f);
        const float breathe = .78f + .22f * wave(t, b.period * .8, b.phase + .1);
        band(paint.scene, {-160, y, canvas::width + 320, b.h}, angle, color,
             b.alpha * strength * breathe);
    }
}

void auroraFloor(Paint paint, double seconds, float amount, Color tint) {
    if (level_ == MotionLevel::Off || amount <= .002f)
        return;
    const bool full = level_ == MotionLevel::Full;
    const double t = full ? seconds : seconds * .66;
    const float y = 700 + 40 * wave(t, 53, .2);
    const float hue = .5f + .5f * wave(t, 97, .6);
    Color color = focus::mix(aura(0x2BD4BD), aura(0x7C6CF0), hue);
    color = focus::mix(color, tint, .2f);
    const float alpha = (full ? .13f : .08f) * focus::clamp01(amount) *
                        (.8f + .2f * wave(t, 37, .4));
    band(paint.scene, {-160, y, canvas::width + 320, canvas::height + 120 - y},
         176 + 6 * wave(t, 71, .1), color, alpha);
}

void motes(Paint paint, double seconds, float amount) {
    if (level_ != MotionLevel::Full || amount <= .002f)
        return;
    Scene &scene = paint.scene;
    constexpr int kCount = 16;
    constexpr float kTravel = canvas::height + 160;
    for (int i = 0; i < kCount; ++i) {
        const float speed = 8 + static_cast<float>((i * 37) % 13);
        const float side = 3 + static_cast<float>(i % 4);
        const float x = std::fmod(static_cast<float>(i) * 733.0f + 97, canvas::width) +
                        26 * wave(seconds, 11 + i % 5, i * .13);
        const float y = canvas::height + 80 -
                        std::fmod(static_cast<float>(seconds) * speed + i * 211.0f, kTravel);
        // Fade in from the bottom edge and out before the top bar.
        const float edge = focus::clamp01(std::min(canvas::height - y, y - 140) / 220);
        const float alpha = (.10f + .05f * (i % 3)) * focus::clamp01(amount) * edge;
        if (alpha <= .004f)
            continue;
        Node &mote = scene.shape({x, y, side, side}, focus::withAlpha(rgb(0xDDE8FF), alpha), side / 2);
        mote.shadow = {0, 0, side * 2.5f, 0, focus::withAlpha(rgb(0xB8D4FF), alpha * .6f)};
    }
}

Transform wallpaperDrift(double seconds, float stackY) {
    if (level_ == MotionLevel::Off)
        return Transform();
    const bool full = level_ == MotionLevel::Full;
    // The scale's margin is 1920 * (s - 1) / 2 across and 1080 * (s - 1) / 2 down; the drift stays
    // under it with room to spare.
    const float scale = full ? 1.05f : 1.03f;
    const float reachX = full ? 26 : 14, reachY = full ? 12 : 7;
    const float depth = full ? std::max(-10.0f, std::min(10.0f, stackY * .02f)) : 0;
    const float dx = reachX * wave(seconds, full ? 41 : 59, .1);
    const float dy = reachY * wave(seconds, full ? 53 : 67, .6) + depth;
    return Transform::scaleAbout(canvas::width / 2, canvas::height / 2, scale, scale, dx, dy);
}
} // namespace kit
} // namespace nativetv
