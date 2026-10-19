#pragma once

#include <stdint.h>
#include <string.h>

// Clockwise quarter-turns of a square RGB565 image, matching M5GFX setRotation.
// The panel window stays at rotation 0; only the pixels move, so the round-glass
// offset does not have to be retuned. Touch samples use the inverse map.

inline void orient_map(int n, int rotation, int x, int y, int *ox, int *oy)
{
    const int last = n - 1;
    switch (rotation & 3) {
    case 1: *ox = last - y; *oy = x; break;       // 90 clockwise
    case 2: *ox = last - x; *oy = last - y; break; // 180
    case 3: *ox = y; *oy = last - x; break;        // 270 clockwise
    default: *ox = x; *oy = y; break;
    }
}

inline void orient_unmap(int n, int rotation, int px, int py, int *lx, int *ly)
{
    const int last = n - 1;
    switch (rotation & 3) {
    case 1: *lx = py; *ly = last - px; break;
    case 2: *lx = last - px; *ly = last - py; break;
    case 3: *lx = last - py; *ly = px; break;
    default: *lx = px; *ly = py; break;
    }
}

// src and dst must not alias unless rotation is 0.
inline void orient_blit(const uint16_t *src, uint16_t *dst, int n, int rotation)
{
    rotation &= 3;
    if (rotation == 0) {
        if (src != dst) memcpy(dst, src, (size_t)n * n * sizeof(uint16_t));
        return;
    }
    if (rotation == 2) {
        for (int y = 0; y < n; ++y) {
            const uint16_t *s = src + (size_t)(n - 1 - y) * n + (n - 1);
            uint16_t *d = dst + (size_t)y * n;
            for (int x = 0; x < n; ++x) *d++ = *s--;
        }
        return;
    }
    // Destination rows stay sequential. 90° reads a column of the source.
    if (rotation == 1) {
        for (int py = 0; py < n; ++py) {
            uint16_t *d = dst + (size_t)py * n;
            for (int px = 0; px < n; ++px) d[px] = src[(size_t)(n - 1 - px) * n + py];
        }
        return;
    }
    for (int py = 0; py < n; ++py) {
        uint16_t *d = dst + (size_t)py * n;
        const int lx = n - 1 - py;
        for (int px = 0; px < n; ++px) d[px] = src[(size_t)px * n + lx];
    }
}
