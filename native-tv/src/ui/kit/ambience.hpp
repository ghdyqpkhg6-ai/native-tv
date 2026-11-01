#pragma once
// Fork: Home's atmosphere - drifting aurora washes, a few motes, the glow under a focused item and
// the drift of a wallpaper - at the level chosen in Home settings ("motion" in the Home layout).
// Off draws exactly what v0.118.0 draws. Everything here is linear gradients and small distance-
// field shapes the renderer already batches; nothing needs a shader or an offscreen pass, and the
// application only asks for frames (at the ambient half rate) while it is on screen and not dimmed.
#include "ui/kit/widgets.hpp"
#include <string>

namespace nativetv {
namespace kit {
enum class MotionLevel { Off, Subtle, Full };
// "off", "subtle" or "full"; anything else is Subtle, the default.
MotionLevel motionLevelOf(const std::string &value);
// Set once per frame by the application from the Home layout.
void setMotionLevel(MotionLevel level);
MotionLevel motionLevel();

// A focus zoom token (motion::scale) as the level wants it: Full zooms a little further.
float zoomTarget(float target);
// A soft light in `tint` around a focused item, drawn under it; nothing at rest or when Off.
void focusGlow(Scene &scene, Rect area, float rounding, Color tint, float focus);
// Slow bands of coloured light behind the page. `amount` is how much of the wall shows (none
// under opaque art), `seconds` the frame clock, `tint` the wall's own colour, and `stackY` the
// rows' scroll, which the bands follow a little for depth.
void aurora(Paint paint, double seconds, float amount, Color tint, float stackY = 0);
// One low band over the bottom of the hero's shade, where the rows sit: it never reaches the
// picture itself.
void auroraFloor(Paint paint, double seconds, float amount, Color tint);
// Full only: a few faint motes rising slowly.
void motes(Paint paint, double seconds, float amount);
// How a full-screen wallpaper drifts: a scale about the centre and a slow offset that stays
// inside the margin the scale makes, so no edge ever shows. Identity when Off.
Transform wallpaperDrift(double seconds, float stackY);
} // namespace kit
} // namespace nativetv
