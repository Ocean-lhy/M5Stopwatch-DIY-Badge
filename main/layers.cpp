#include "layers.h"

#include <stdio.h>
#include <stdlib.h>

#include "badge_config.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "ui_theme.h"

static const char *TAG = "badge_layers";

static bool load_jpeg(M5Canvas &dst, const char *path, int w, int h)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        dst.deleteSprite();
        return false;
    }
    fseek(file, 0, SEEK_END);
    long len = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (len <= 0 || len > 512 * 1024) {
        fclose(file);
        dst.deleteSprite();
        return false;
    }
    auto *buf = static_cast<uint8_t *>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM));
    if (!buf) {
        fclose(file);
        return false;
    }
    size_t got = fread(buf, 1, len, file);
    fclose(file);

    bool ok = false;
    if (got == (size_t)len) {
        if (!dst.getBuffer() || dst.width() != w || dst.height() != h) {
            dst.deleteSprite();
            dst.setPsram(true);
            dst.setColorDepth(16);
            dst.createSprite(w, h);
        }
        if (dst.getBuffer()) {
            dst.fillSprite(TFT_BLACK);
            ok = dst.drawJpg(buf, len, 0, 0, w, h);
        }
    }
    heap_caps_free(buf);
    if (!ok) {
        ESP_LOGW(TAG, "failed to decode %s", path);
        dst.deleteSprite();
    }
    return ok;
}

void Layers::reload(uint8_t mask)
{
    if (mask & BADGE_LAYER_CARD) load_jpeg(card, kLayerCardPath, ui::kSize, ui::kSize);
    if (mask & BADGE_LAYER_ABOUT) load_jpeg(about, kLayerAboutPath, ui::kSize, ui::kSize);
    if (mask & BADGE_LAYER_NAME) load_jpeg(name, kLayerNamePath, kNameStripW, kNameStripH);
}
