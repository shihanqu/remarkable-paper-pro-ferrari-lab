#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <algorithm>
#include "qtfb_protocol.h"

// Little-endian RGBA8888: byte[0]=R, byte[1]=G, byte[2]=B, byte[3]=A
static inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) {
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline uint32_t rgbLerp(uint32_t c1, uint32_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    int r1 = c1 & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = (c1 >> 16) & 0xFF;
    int r2 = c2 & 0xFF, g2 = (c2 >> 8) & 0xFF, b2 = (c2 >> 16) & 0xFF;
    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int b = b1 + (int)((b2 - b1) * t);
    return rgba(r, g, b, 255);
}

static inline uint32_t hsvToRgba(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }
    return rgba((uint8_t)((r + m) * 255.0f), (uint8_t)((g + m) * 255.0f), (uint8_t)((b + m) * 255.0f));
}

// 8x8 Bayer ordered dithering matrix for Gallery 3 E-Ink color quantization
static const uint8_t BAYER8[8][8] = {
    {  0, 32,  8, 40,  2, 34, 10, 42 },
    { 48, 16, 56, 24, 50, 18, 58, 26 },
    { 12, 44,  4, 36, 14, 46,  6, 38 },
    { 60, 28, 52, 20, 62, 30, 54, 22 },
    {  3, 35, 11, 43,  1, 33,  9, 41 },
    { 51, 19, 59, 27, 49, 17, 57, 25 },
    { 15, 47,  7, 39, 13, 45,  5, 37 },
    { 63, 31, 55, 23, 61, 29, 53, 21 }
};

static inline uint32_t ditherGallery3(int x, int y, uint8_t r, uint8_t g, uint8_t b) {
    int threshold = ((int)BAYER8[y & 7][x & 7] - 32) / 2;
    int dr = std::clamp((int)r + threshold, 0, 255);
    int dg = std::clamp((int)g + threshold, 0, 255);
    int db = std::clamp((int)b + threshold, 0, 255);
    // Quantize to 6 levels per channel (Gallery 3 friendly crisp step palette)
    uint8_t qr = (uint8_t)(((dr * 5 + 127) / 255) * 51);
    uint8_t qg = (uint8_t)(((dg * 5 + 127) / 255) * 51);
    uint8_t qb = (uint8_t)(((db * 5 + 127) / 255) * 51);
    return rgba(qr, qg, qb);
}

// Standard 8x8 ASCII bitmap font (chars 32..126)
static const uint8_t FONT8x8[95][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // '!'
    {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // '"'
    {0x6C,0x6C,0xFE,0x6C,0xFE,0x6C,0x6C,0x00}, // '#'
    {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // '$'
    {0x00,0xC6,0xCC,0x18,0x30,0x66,0xC6,0x00}, // '%'
    {0x38,0x6C,0x38,0x76,0xDC,0xCC,0x76,0x00}, // '&'
    {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // '''
    {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // '('
    {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // ')'
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // '*'
    {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // '+'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // ','
    {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // '-'
    {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // '.'
    {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // '/'
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // '0'
    {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // '1'
    {0x3C,0x66,0x06,0x1C,0x30,0x66,0x7E,0x00}, // '2'
    {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // '3'
    {0x0E,0x1E,0x36,0x66,0x7F,0x06,0x0F,0x00}, // '4'
    {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // '5'
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // '6'
    {0x7E,0x66,0x06,0x0C,0x18,0x18,0x18,0x00}, // '7'
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // '8'
    {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // '9'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x00}, // ':'
    {0x00,0x18,0x18,0x00,0x00,0x18,0x18,0x30}, // ';'
    {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // '<'
    {0x00,0x00,0x7E,0x00,0x00,0x7E,0x00,0x00}, // '='
    {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // '>'
    {0x3C,0x66,0x06,0x0C,0x18,0x00,0x18,0x00}, // '?'
    {0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00}, // '@'
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // 'A'
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // 'B'
    {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // 'C'
    {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // 'D'
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // 'E'
    {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // 'F'
    {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}, // 'G'
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // 'H'
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'I'
    {0x1E,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00}, // 'J'
    {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // 'K'
    {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // 'L'
    {0xC6,0xEE,0xFE,0xFE,0xD6,0xC6,0xC6,0x00}, // 'M'
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // 'N'
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 'O'
    {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // 'P'
    {0x3C,0x66,0x66,0x66,0x6A,0x6C,0x36,0x00}, // 'Q'
    {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // 'R'
    {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // 'S'
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 'T'
    {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 'U'
    {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // 'V'
    {0xC6,0xC6,0xD6,0xFE,0xFE,0xEE,0xC6,0x00}, // 'W'
    {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // 'X'
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // 'Y'
    {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // 'Z'
    {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // '['
    {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // '\'
    {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // ']'
    {0x10,0x38,0x6C,0xC6,0x00,0x00,0x00,0x00}, // '^'
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // '_'
    {0x30,0x18,0x0C,0x00,0x00,0x00,0x00,0x00}, // '`'
    {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // 'a'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // 'b'
    {0x00,0x00,0x3C,0x66,0x60,0x66,0x3C,0x00}, // 'c'
    {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // 'd'
    {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // 'e'
    {0x1C,0x36,0x30,0x78,0x30,0x30,0x30,0x00}, // 'f'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // 'g'
    {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // 'h'
    {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // 'i'
    {0x06,0x00,0x0E,0x06,0x06,0x66,0x66,0x3C}, // 'j'
    {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // 'k'
    {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 'l'
    {0x00,0x00,0xEC,0xFE,0xD6,0xC6,0xC6,0x00}, // 'm'
    {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // 'n'
    {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // 'o'
    {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // 'p'
    {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // 'q'
    {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // 'r'
    {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // 's'
    {0x18,0x18,0x7E,0x18,0x18,0x18,0x0E,0x00}, // 't'
    {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // 'u'
    {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // 'v'
    {0x00,0x00,0xC6,0xC6,0xD6,0xFE,0x6C,0x00}, // 'w'
    {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // 'x'
    {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // 'y'
    {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // 'z'
    {0x0E,0x18,0x18,0x70,0x18,0x18,0x0E,0x00}, // '{'
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // '|'
    {0x70,0x18,0x18,0x0E,0x18,0x18,0x70,0x00}, // '}'
    {0x76,0xDC,0x00,0x00,0x00,0x00,0x00,0x00}  // '~'
};

class Canvas2D {
public:
    uint32_t *fb = nullptr;
    int W = RMPP_WIDTH;
    int H = RMPP_HEIGHT;

    Canvas2D(uint32_t *buffer, int w = RMPP_WIDTH, int h = RMPP_HEIGHT)
        : fb(buffer), W(w), H(h) {}

    inline void putPixel(int x, int y, uint32_t color) {
        if ((unsigned)x < (unsigned)W && (unsigned)y < (unsigned)H) {
            fb[y * W + x] = color;
        }
    }

    inline void blendPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b, float alpha) {
        if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H || alpha <= 0.0f) return;
        if (alpha >= 1.0f) {
            fb[y * W + x] = rgba(r, g, b, 255);
            return;
        }
        uint32_t dst = fb[y * W + x];
        int dr = dst & 0xFF, dg = (dst >> 8) & 0xFF, db = (dst >> 16) & 0xFF;
        int nr = dr + (int)((r - dr) * alpha);
        int ng = dg + (int)((g - dg) * alpha);
        int nb = db + (int)((b - db) * alpha);
        fb[y * W + x] = rgba(nr, ng, nb, 255);
    }

    void clear(uint32_t color) {
        int total = W * H;
        for (int i = 0; i < total; i++) fb[i] = color;
    }

    void fillRect(int x, int y, int w, int h, uint32_t color) {
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(W, x + w), y1 = std::min(H, y + h);
        for (int py = y0; py < y1; py++) {
            uint32_t *row = fb + py * W;
            for (int px = x0; px < x1; px++) row[px] = color;
        }
    }

    void fillDitherGradientRect(int x, int y, int w, int h, uint32_t topCol, uint32_t botCol) {
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(W, x + w), y1 = std::min(H, y + h);
        int r1 = topCol & 0xFF, g1 = (topCol >> 8) & 0xFF, b1 = (topCol >> 16) & 0xFF;
        int r2 = botCol & 0xFF, g2 = (botCol >> 8) & 0xFF, b2 = (botCol >> 16) & 0xFF;
        for (int py = y0; py < y1; py++) {
            float t = (h > 1) ? (float)(py - y) / (float)(h - 1) : 0.0f;
            uint8_t r = (uint8_t)(r1 + (r2 - r1) * t);
            uint8_t g = (uint8_t)(g1 + (g2 - g1) * t);
            uint8_t b = (uint8_t)(b1 + (b2 - b1) * t);
            uint32_t *row = fb + py * W;
            for (int px = x0; px < x1; px++) {
                row[px] = ditherGallery3(px, py, r, g, b);
            }
        }
    }

    void drawRect(int x, int y, int w, int h, int thickness, uint32_t color) {
        fillRect(x, y, w, thickness, color);
        fillRect(x, y + h - thickness, w, thickness, color);
        fillRect(x, y, thickness, h, color);
        fillRect(x + w - thickness, y, thickness, h, color);
    }

    void fillRoundRect(int x, int y, int w, int h, int radius, uint32_t fillCol, uint32_t borderCol = 0, int borderThick = 0) {
        if (radius <= 0) {
            fillRect(x, y, w, h, fillCol);
            if (borderThick > 0) drawRect(x, y, w, h, borderThick, borderCol);
            return;
        }
        int x0 = std::max(0, x), y0 = std::max(0, y);
        int x1 = std::min(W, x + w), y1 = std::min(H, y + h);
        int r = std::min(radius, std::min(w / 2, h / 2));
        int r2 = r * r;
        int ri = std::max(0, r - borderThick);
        int ri2 = ri * ri;

        for (int py = y0; py < y1; py++) {
            uint32_t *row = fb + py * W;
            int dy = 0;
            if (py < y + r) dy = (y + r) - py;
            else if (py >= y + h - r) dy = py - (y + h - r - 1);

            for (int px = x0; px < x1; px++) {
                int dx = 0;
                if (px < x + r) dx = (x + r) - px;
                else if (px >= x + w - r) dx = px - (x + w - r - 1);

                if (dx > 0 && dy > 0) {
                    int d2 = dx * dx + dy * dy;
                    if (d2 > r2) continue;
                    if (borderThick > 0 && d2 >= ri2) {
                        row[px] = borderCol;
                    } else {
                        row[px] = fillCol;
                    }
                } else {
                    if (borderThick > 0 &&
                        (px < x + borderThick || px >= x + w - borderThick ||
                         py < y + borderThick || py >= y + h - borderThick)) {
                        row[px] = borderCol;
                    } else {
                        row[px] = fillCol;
                    }
                }
            }
        }
    }

    void fillCircle(int cx, int cy, int r, uint32_t color) {
        int x0 = std::max(0, cx - r), y0 = std::max(0, cy - r);
        int x1 = std::min(W - 1, cx + r), y1 = std::min(H - 1, cy + r);
        int r2 = r * r;
        for (int py = y0; py <= y1; py++) {
            int dy = py - cy;
            int dy2 = dy * dy;
            uint32_t *row = fb + py * W;
            for (int px = x0; px <= x1; px++) {
                int dx = px - cx;
                if (dx * dx + dy2 <= r2) row[px] = color;
            }
        }
    }

    void drawCircle(int cx, int cy, int r, int thickness, uint32_t color) {
        int ro = r + thickness / 2;
        int ri = std::max(0, r - (thickness - thickness / 2));
        int x0 = std::max(0, cx - ro), y0 = std::max(0, cy - ro);
        int x1 = std::min(W - 1, cx + ro), y1 = std::min(H - 1, cy + ro);
        int ro2 = ro * ro, ri2 = ri * ri;
        for (int py = y0; py <= y1; py++) {
            int dy = py - cy;
            int dy2 = dy * dy;
            uint32_t *row = fb + py * W;
            for (int px = x0; px <= x1; px++) {
                int dx = px - cx;
                int d2 = dx * dx + dy2;
                if (d2 <= ro2 && d2 >= ri2) row[px] = color;
            }
        }
    }

    void drawLine(int x0, int y0, int x1, int y1, int thickness, uint32_t color) {
        int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int err = dx + dy;
        int rad = thickness / 2;
        while (true) {
            if (rad <= 0) putPixel(x0, y0, color);
            else if (rad == 1) fillRect(x0 - 1, y0 - 1, 3, 3, color);
            else fillCircle(x0, y0, rad, color);
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    // Compute proportional glyph metrics (leftmost bit column and width in columns)
    static inline void glyphBounds(char c, int &colStart, int &colWidth) {
        if (c == ' ') { colStart = 0; colWidth = 4; return; }
        if (c < 32 || c > 126) c = '?';
        const uint8_t *glyph = FONT8x8[c - 32];
        uint8_t mask = 0;
        for (int r = 0; r < 8; r++) mask |= glyph[r];
        if (!mask) { colStart = 0; colWidth = 4; return; }
        int left = 0;
        while (left < 8 && !(mask & (0x80 >> left))) left++;
        int right = 7;
        while (right > left && !(mask & (0x80 >> right))) right--;
        colStart = left;
        colWidth = (right - left + 1);
    }

    static inline int charAdvance(char c, int scale, bool condensed = false) {
        int colStart, colW;
        glyphBounds(c, colStart, colW);
        int gap = condensed ? std::max(1, scale - 1) : (scale >= 3 ? 3 : 2);
        return colW * scale + gap;
    }

    void drawCharClipped(int x, int y, char c, int scale, uint32_t color, int clipX0, int clipX1) {
        if (c < 32 || c > 126) c = '?';
        int colStart, colW;
        glyphBounds(c, colStart, colW);
        const uint8_t *glyph = FONT8x8[c - 32];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            if (!bits) continue;
            for (int col = colStart; col < colStart + colW; col++) {
                if (bits & (0x80 >> col)) {
                    int px = x + (col - colStart) * scale;
                    int py = y + row * scale;
                    for (int sy = 0; sy < scale; sy++) {
                        for (int sx = 0; sx < scale; sx++) {
                            int fx = px + sx;
                            if (fx >= clipX0 && fx < clipX1) {
                                putPixel(fx, py + sy, color);
                            }
                        }
                    }
                }
            }
        }
    }

    int textWidth(const char *str, int scale, bool condensed = false) {
        int w = 0;
        for (const char *p = str; *p; p++) {
            if (*p == '\n') break;
            w += charAdvance(*p, scale, condensed);
        }
        return w;
    }

    void drawText(int x, int y, const char *str, int scale, uint32_t color, bool bold = false, int maxW = 0) {
        int clipX0 = 0;
        int clipX1 = (maxW > 0) ? std::min(W, x + maxW) : W;
        bool condensed = false;
        int effScale = scale;

        if (maxW > 0) {
            if (textWidth(str, effScale, false) > maxW) {
                condensed = true;
            }
            while (effScale > 1 && textWidth(str, effScale, true) > maxW) {
                effScale--;
            }
        }

        int cx = x;
        for (const char *p = str; *p; p++) {
            if (*p == '\n') {
                cx = x;
                y += 10 * effScale;
                continue;
            }
            int adv = charAdvance(*p, effScale, condensed);
            if (cx + adv > clipX1 + 2) break;
            drawCharClipped(cx, y, *p, effScale, color, clipX0, clipX1);
            if (bold) drawCharClipped(cx + 1, y, *p, effScale, color, clipX0, clipX1);
            cx += adv;
        }
    }

    // Crisp 11px-tall compact font (between scale=1 8px and scale=2 16px) for compact HUD readouts
    void drawCompactText(int x, int y, const char *str, uint32_t color, bool bold = false, int maxW = 0) {
        int clipX1 = (maxW > 0) ? std::min(W, x + maxW) : W;
        static const int rowYOff[8] = {0, 2, 3, 4, 6, 7, 8, 10};
        static const int rowH[8]    = {2, 1, 1, 2, 1, 1, 2, 1}; // Total = 11px tall
        int cx = x;
        for (const char *p = str; *p; p++) {
            char c = *p;
            if (c == ' ') {
                cx += 5;
                continue;
            }
            if (c < 32 || c > 126) c = '?';
            int colStart, colW;
            glyphBounds(c, colStart, colW);
            int charW = colW + (bold ? 1 : 0);
            if (cx + charW > clipX1) break;
            const uint8_t *glyph = FONT8x8[c - 32];
            for (int r = 0; r < 8; r++) {
                uint8_t bits = glyph[r];
                if (!bits) continue;
                int py = y + rowYOff[r];
                int rh = rowH[r];
                for (int col = colStart; col < colStart + colW; col++) {
                    if (bits & (0x80 >> col)) {
                        int px = cx + (col - colStart);
                        for (int dy = 0; dy < rh; dy++) {
                            putPixel(px, py + dy, color);
                            if (bold) putPixel(px + 1, py + dy, color);
                        }
                    }
                }
            }
            cx += charW + 2;
        }
    }

    void drawTextCentered(int rx, int ry, int rw, int rh, const char *str, int scale, uint32_t color, bool bold = false) {
        int pad = 8;
        int availW = std::max(16, rw - pad * 2);
        int effScale = scale;
        bool condensed = false;

        if (textWidth(str, effScale, false) > availW) {
            condensed = true;
        }
        while (effScale > 1 && textWidth(str, effScale, true) > availW) {
            effScale--;
        }

        int tw = textWidth(str, effScale, condensed);
        int th = 8 * effScale;
        int tx = rx + std::max(4, (rw - tw) / 2);
        int ty = ry + (rh - th) / 2;

        int clipX0 = std::max(0, rx + 3);
        int clipX1 = std::min(W, rx + rw - 3);
        int cx = tx;
        for (const char *p = str; *p; p++) {
            int adv = charAdvance(*p, effScale, condensed);
            if (cx + adv > clipX1 + 2) break;
            drawCharClipped(cx, ty, *p, effScale, color, clipX0, clipX1);
            if (bold) drawCharClipped(cx + 1, ty, *p, effScale, color, clipX0, clipX1);
            cx += adv;
        }
    }

    // Flat/Dithered 3D triangle rasterizer with bounding-box clipping
    void fillTriangleDither(int x0, int y0, int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, int clipX0, int clipY0, int clipX1, int clipY1) {
        int minX = std::max(clipX0, std::min({x0, x1, x2}));
        int maxX = std::min(clipX1 - 1, std::max({x0, x1, x2}));
        int minY = std::max(clipY0, std::min({y0, y1, y2}));
        int maxY = std::min(clipY1 - 1, std::max({y0, y1, y2}));
        if (minX > maxX || minY > maxY) return;

        auto edge = [](int ax, int ay, int bx, int by, int px, int py) {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        };
        int area = edge(x0, y0, x1, y1, x2, y2);
        if (area == 0) return;

        for (int py = minY; py <= maxY; py++) {
            uint32_t *row = fb + py * W;
            for (int px = minX; px <= maxX; px++) {
                int w0 = edge(x1, y1, x2, y2, px, py);
                int w1 = edge(x2, y2, x0, y0, px, py);
                int w2 = edge(x0, y0, x1, y1, px, py);
                if ((area > 0 && w0 >= 0 && w1 >= 0 && w2 >= 0) ||
                    (area < 0 && w0 <= 0 && w1 <= 0 && w2 <= 0)) {
                    row[px] = ditherGallery3(px, py, r, g, b);
                }
            }
        }
    }
};

struct UIButton {
    int x, y, w, h;
    bool contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};
