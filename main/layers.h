#pragma once

#include <stdint.h>

#include <M5GFX.h>

constexpr int kNameStripW = 360;
constexpr int kNameStripH = 96;

constexpr char kLayerCardPath[] = "/spiffs/card.jpg";
constexpr char kLayerAboutPath[] = "/spiffs/about.jpg";
constexpr char kLayerNamePath[] = "/spiffs/name.jpg";
constexpr char kAvatarPath[] = "/spiffs/avatar.jpg";

// Decoded copies of the editor-rendered layers, kept in PSRAM so page changes
// never touch the filesystem or the JPEG decoder.
struct Layers {
    M5Canvas card;
    M5Canvas about;
    M5Canvas name;

    // Reloads the layers selected by `mask` (BadgeLayerFlags); missing files free the sprite.
    void reload(uint8_t mask);
};
