#pragma once

#include <stdint.h>

// Design tokens for the 466x466 round AMOLED. Everything outside the circle of
// radius kRadius is not visible glass, so layouts are derived from the center.
namespace ui {

constexpr int kSize = 466;
constexpr int kCenter = 233;
constexpr int kRadius = 233;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Theme {
    uint16_t bg;
    uint16_t ink;
    uint16_t muted;
    uint16_t accent;
    uint16_t dim;   // inactive dots, tracks
};

// Matches the presets in web/app.js (THEMES).
constexpr Theme kThemeOcean = {rgb565(0, 0, 0), rgb565(242, 246, 250), rgb565(138, 155, 176),
                               rgb565(59, 167, 255), rgb565(28, 40, 54)};
constexpr Theme kThemeBrand = {rgb565(3, 36, 71), rgb565(255, 255, 255), rgb565(160, 196, 230),
                               rgb565(127, 208, 255), rgb565(24, 64, 104)};
constexpr Theme kThemePaper = {rgb565(244, 241, 234), rgb565(26, 26, 26), rgb565(110, 106, 98),
                               rgb565(224, 84, 46), rgb565(214, 208, 196)};

// The scan page is always black with a white plate, regardless of theme, for
// maximum QR contrast and minimum AMOLED power.
constexpr uint16_t kScanBg = rgb565(0, 0, 0);
constexpr uint16_t kPlate = rgb565(255, 255, 255);
constexpr uint16_t kAmbientInk = rgb565(150, 160, 172);
constexpr uint16_t kAmbientDim = rgb565(70, 76, 84);
constexpr uint16_t kOk = rgb565(52, 199, 89);
constexpr uint16_t kWarn = rgb565(255, 159, 10);
constexpr uint16_t kLow = rgb565(255, 69, 58);

// Layout
constexpr int kStatusY = 40;          // time + battery row on card/about
constexpr int kDotRingRadius = 222;   // countdown dot ring
constexpr int kDotCount = 60;
constexpr int kPlateSize = 300;       // scan page QR plate (fits the circle with the label below)
constexpr int kPlateY = 64;           // plate top on the scan page
constexpr int kPlateRadius = 28;
constexpr int kSetupPlate = 240;
constexpr int kSetupPlateY = 86;

// Motion (ms)
constexpr int kSlideMs = 260;
constexpr int kScanInMs = 280;
constexpr int kDipOutMs = 140;
constexpr int kDipInMs = 200;

// Brightness (CO5300 0..255)
constexpr uint8_t kScanBrightness = 255;

} // namespace ui
