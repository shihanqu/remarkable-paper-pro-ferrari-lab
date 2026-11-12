#pragma once
#include <stdint.h>
#include <math.h>
#include <vector>
#include <algorithm>
#include "gfx2d.h"

struct Vec3 {
    float x, y, z;
};

struct Face3D {
    int i0, i1, i2;
    float depth;
    uint8_t r, g, b;
};

struct Particle {
    float x, y, vx, vy;
    uint8_t hue;
};

class ShowcaseStudio {
public:
    int activeDemo = 0; // 0=3D Wave Fabric, 1=3D/4D Polyhedra, 2=Particle Vortex, 3=Calligrapher & Botanicals
    UIButton btnDemoSelect[4];
    UIButton btnAction[4];
    UIButton btnColorSelect[8];

    // Full-height Portrait Viewport bounds on the 1620x2160 screen (reaches y = 2140)
    static const int VX = 24;
    static const int VY = 264;
    static const int VW = 1572;
    static const int VH = 1876; // VY + VH = 2140

    // Last pointer state
    bool pointerDown = false;
    int ptrX = VX + VW / 2;
    int ptrY = VY + VH / 2;
    int prevPtrX = -1;
    int prevPtrY = -1;
    float normPressure = 0.6f; // 0..1

    // --- Demo 0: 3D Elastic Wave Fabric (34 x 34 grid) ---
    static const int GRID_N = 34;
    float waveH[GRID_N][GRID_N];
    float waveV[GRID_N][GRID_N];
    float waveTime = 0.0f;

    // --- Demo 1: 3D Shaded Polyhedra & 4D Tesseract ---
    int polyShape = 0; // 0=Torus, 1=4D Tesseract, 2=Geodesic Sphere, 3=Trefoil Knot
    float rotX = 0.45f, rotY = 0.65f, rotW = 0.0f;
    float autoSpin = 1.0f;

    // --- Demo 2: Particle Vortex Symphony ---
    static const int NUM_PARTICLES = 2200;
    std::vector<Particle> particles;
    int vortexMode = 0; // 0=Orbital Attractor, 1=Dual Galaxy, 2=Repulsor Shockwave

    // --- Demo 3: Master Calligrapher & Botanical Ink Studio ---
    int brushMode = 0; // 0=Calligraphy Nib, 1=Chisel Tilt, 2=Prismatic Ribbon, 3=Botanical Bloom
    int selectedColorIdx = 0;
    std::vector<uint32_t> artCanvas;
    bool artInitialized = false;

    ShowcaseStudio() : particles(NUM_PARTICLES), artCanvas(VW * VH, rgba(250, 249, 244)) {
        resetWaveGrid();
        seedWaveShowcase();
        initParticles();
        initArtCanvas();
    }

    void resetWaveGrid() {
        for (int y = 0; y < GRID_N; y++) {
            for (int x = 0; x < GRID_N; x++) {
                waveH[y][x] = 0.0f;
                waveV[y][x] = 0.0f;
            }
        }
    }

    void seedWaveShowcase() {
        for (int y = 0; y < GRID_N; y++) {
            for (int x = 0; x < GRID_N; x++) {
                float dx = (float)x - GRID_N * 0.5f;
                float dy = (float)y - GRID_N * 0.5f;
                float r = sqrtf(dx * dx + dy * dy);
                waveH[y][x] = 95.0f * cosf(r * 0.55f) * expf(-r * 0.08f);
            }
        }
    }

    void impulseWave(float gx, float gy, float strength, float radius) {
        for (int y = 1; y < GRID_N - 1; y++) {
            for (int x = 1; x < GRID_N - 1; x++) {
                float dx = (float)x - gx;
                float dy = (float)y - gy;
                float d2 = dx * dx + dy * dy;
                if (d2 < radius * radius) {
                    waveH[y][x] += strength * expf(-d2 / (radius * 0.6f));
                }
            }
        }
    }

    // Exact inverse of drawWaveFabricDemo's 45-deg isometric projection:
    //   isoX = (fx - fy) * scaleX,  isoY = (fx + fy) * scaleY
    //   where fx = gx - (GRID_N - 1)*0.5, fy = gy - (GRID_N - 1)*0.5
    void screenToWaveGrid(int sx, int sy, float &gx, float &gy) const {
        const float cx = (float)(VX + VW / 2);
        const float cy = (float)(VY + VH / 2 + 60);
        const float scaleX = 22.5f;
        const float scaleY = 14.5f;
        const float halfN = (GRID_N - 1) * 0.5f;

        float u = ((float)sx - cx) / scaleX; // fx - fy
        float v = ((float)sy - cy) / scaleY; // fx + fy
        float fx = 0.5f * (u + v);
        float fy = 0.5f * (v - u);

        gx = std::clamp(fx + halfN, 1.5f, (float)(GRID_N - 2) - 0.5f);
        gy = std::clamp(fy + halfN, 1.5f, (float)(GRID_N - 2) - 0.5f);
    }

    void applyTouchWaveAt(int sx, int sy, float strengthScale = 1.0f) {
        if (sx < VX || sx >= VX + VW || sy < VY + 90 || sy >= VY + VH) return;
        float gx = 0.0f, gy = 0.0f;
        screenToWaveGrid(sx, sy, gx, gy);
        impulseWave(gx, gy, -58.0f * (0.45f + normPressure) * strengthScale, 3.2f);
    }

    void stepWavePhysics() {
        waveTime += 0.08f;
        if (pointerDown && ptrX >= VX && ptrX < VX + VW && ptrY >= VY + 90 && ptrY < VY + VH) {
            applyTouchWaveAt(ptrX, ptrY, 1.0f);
        }
        float nextV[GRID_N][GRID_N];
        for (int y = 1; y < GRID_N - 1; y++) {
            for (int x = 1; x < GRID_N - 1; x++) {
                float lap = (waveH[y - 1][x] + waveH[y + 1][x] + waveH[y][x - 1] + waveH[y][x + 1]) * 0.25f - waveH[y][x];
                nextV[y][x] = (waveV[y][x] + lap * 0.42f) * 0.965f;
            }
        }
        for (int y = 1; y < GRID_N - 1; y++) {
            for (int x = 1; x < GRID_N - 1; x++) {
                waveV[y][x] = nextV[y][x];
                waveH[y][x] = std::clamp(waveH[y][x] + waveV[y][x], -160.0f, 160.0f);
            }
        }
    }

    void initParticles() {
        for (int i = 0; i < NUM_PARTICLES; i++) {
            float ang = (float)i * 0.1375f;
            float rad = 40.0f + (float)(i % 660);
            particles[i].x = VX + VW * 0.5f + cosf(ang) * rad;
            particles[i].y = VY + VH * 0.52f + sinf(ang) * rad;
            particles[i].vx = -sinf(ang) * 4.5f;
            particles[i].vy =  cosf(ang) * 4.5f;
            particles[i].hue = (uint8_t)((i * 360) / NUM_PARTICLES);
        }
    }

    void stepParticles(const RawMarkerState &marker) {
        float ax = (pointerDown && ptrX >= VX && ptrY >= VY) ? (float)ptrX : (VX + VW * 0.5f);
        float ay = (pointerDown && ptrX >= VX && ptrY >= VY) ? (float)ptrY : (VY + VH * 0.52f);
        float pull = pointerDown ? (1.8f + normPressure * 3.5f) : 0.95f;
        if (marker.toolRubber || vortexMode == 2) pull = -2.8f;

        float tiltWindX = (float)marker.tiltX * 0.0005f;
        float tiltWindY = (float)marker.tiltY * 0.0005f;

        for (int i = 0; i < NUM_PARTICLES; i++) {
            Particle &p = particles[i];
            float dx = ax - p.x;
            float dy = ay - p.y;
            float d2 = dx * dx + dy * dy + 180.0f;
            float invD = 1.0f / sqrtf(d2);
            float ux = dx * invD;
            float uy = dy * invD;

            float fRadial = pull * 65.0f * invD;
            float fSwirl  = 42.0f * invD;
            p.vx = (p.vx + ux * fRadial - uy * fSwirl + tiltWindX) * 0.96f;
            p.vy = (p.vy + uy * fRadial + ux * fSwirl + tiltWindY) * 0.96f;

            if (vortexMode == 1) {
                float dx2 = (VX + VW * 0.3f) - p.x;
                float dy2 = (VY + VH * 0.35f) - p.y;
                float invD2 = 1.0f / sqrtf(dx2 * dx2 + dy2 * dy2 + 250.0f);
                p.vx += (-dy2 * invD2 * 25.0f + dx2 * invD2 * 30.0f) * invD2;
                p.vy += ( dx2 * invD2 * 25.0f + dy2 * invD2 * 30.0f) * invD2;
            }

            p.x += p.vx;
            p.y += p.vy;

            if (p.x < VX + 12) { p.x = VX + 12; p.vx = -p.vx * 0.8f; }
            if (p.x >= VX + VW - 12) { p.x = VX + VW - 13; p.vx = -p.vx * 0.8f; }
            if (p.y < VY + 102) { p.y = VY + 102; p.vy = -p.vy * 0.8f; }
            if (p.y >= VY + VH - 12) { p.y = VY + VH - 13; p.vy = -p.vy * 0.8f; }
        }
    }

    uint32_t getPaletteColor(int idx, int step = 0) const {
        static const uint32_t pal[7] = {
            rgba(20, 22, 28),    // Carbon Black
            rgba(215, 30, 40),   // Crimson Red
            rgba(25, 75, 215),   // Cobalt Blue
            rgba(20, 155, 70),   // Emerald Green
            rgba(225, 125, 15),  // Amber Gold
            rgba(175, 35, 165),  // Royal Magenta
            rgba(15, 155, 195)   // Cerulean Cyan
        };
        if (idx < 7) return pal[idx];
        return hsvToRgba((float)((step * 7) % 360), 0.90f, 0.92f);
    }

    void drawBotanicalBranch(Canvas2D &art, float x, float y, float len, float angle, int depth, uint32_t trunkCol, uint32_t bloomCol) {
        if (depth <= 0 || len < 3.0f) {
            art.fillCircle((int)x, (int)y, 6, bloomCol);
            art.fillCircle((int)x, (int)y, 3, rgba(250, 215, 40));
            return;
        }
        float nx = x + cosf(angle) * len;
        float ny = y + sinf(angle) * len;
        int thick = std::max(1, depth);
        art.drawLine((int)x, (int)y, (int)nx, (int)ny, thick, depth <= 2 ? rgba(30, 145, 65) : trunkCol);

        drawBotanicalBranch(art, nx, ny, len * 0.73f, angle - 0.44f, depth - 1, trunkCol, bloomCol);
        drawBotanicalBranch(art, nx, ny, len * 0.73f, angle + 0.40f, depth - 1, trunkCol, bloomCol);
        if (depth >= 4) {
            drawBotanicalBranch(art, (x + nx) * 0.5f, (y + ny) * 0.5f, len * 0.48f, angle + 0.75f, depth - 2, trunkCol, bloomCol);
        }
    }

    void initArtCanvas() {
        std::fill(artCanvas.begin(), artCanvas.end(), rgba(252, 250, 245));
        Canvas2D art(artCanvas.data(), VW, VH);
        for (int y = 120; y < VH - 20; y += 40) {
            for (int x = 30; x < VW - 30; x += 40) {
                art.fillRect(x, y, 2, 2, rgba(195, 195, 190));
            }
        }
        drawBotanicalBranch(art, VW * 0.76f, VH - 60.0f, 155.0f, -1.5708f, 7, rgba(55, 38, 28), rgba(215, 40, 75));
        drawBotanicalBranch(art, VW * 0.25f, VH - 60.0f, 130.0f, -1.52f, 6, rgba(40, 50, 70), rgba(35, 110, 220));
        artInitialized = true;
    }

    void paintStrokeSegment(int x0, int y0, int x1, int y1, float pressure, const RawMarkerState &marker) {
        int ax0 = x0 - VX, ay0 = y0 - VY;
        int ax1 = x1 - VX, ay1 = y1 - VY;
        if (ay0 < 100 && ay1 < 100) return;

        Canvas2D art(artCanvas.data(), VW, VH);
        if (marker.toolRubber) {
            art.drawLine(ax0, ay0, ax1, ay1, 36, rgba(252, 250, 245));
            return;
        }

        static int strokeStep = 0;
        strokeStep++;
        uint32_t col = getPaletteColor(selectedColorIdx, strokeStep);

        float tiltRad = atan2f((float)marker.tiltY, (float)marker.tiltX);
        float tiltMag = sqrtf((float)(marker.tiltX * marker.tiltX + marker.tiltY * marker.tiltY));
        if (tiltMag < 10.0f) tiltRad = 0.65f;

        int dist = (int)sqrtf((float)((ax1 - ax0) * (ax1 - ax0) + (ay1 - ay0) * (ay1 - ay0)));
        int steps = std::max(1, dist / 2);

        for (int s = 0; s <= steps; s++) {
            float t = (float)s / (float)steps;
            int px = ax0 + (int)((ax1 - ax0) * t);
            int py = ay0 + (int)((ay1 - ay0) * t);

            if (brushMode == 0) {
                int rad = std::clamp((int)(2.0f + pressure * 16.0f), 2, 22);
                art.fillCircle(px, py, rad, col);
                if (pressure > 0.72f && (strokeStep + s) % 9 == 0) {
                    int sx = px + ((strokeStep * 37 + s * 13) % 45) - 22;
                    int sy = py + ((strokeStep * 53 + s * 29) % 45) - 22;
                    art.fillCircle(sx, sy, 2 + (s % 4), col);
                }
            } else if (brushMode == 1) {
                float span = 12.0f + pressure * 24.0f;
                int dx = (int)(cosf(tiltRad) * span);
                int dy = (int)(sinf(tiltRad) * span);
                art.drawLine(px - dx, py - dy, px + dx, py + dy, 4, col);
            } else if (brushMode == 2) {
                float span = 14.0f + pressure * 26.0f;
                int dx = (int)(cosf(tiltRad) * span);
                int dy = (int)(sinf(tiltRad) * span);
                uint32_t c1 = hsvToRgba((float)((strokeStep * 4 + s) % 360), 0.9f, 0.95f);
                uint32_t c2 = hsvToRgba((float)((strokeStep * 4 + s + 120) % 360), 0.9f, 0.45f);
                art.drawLine(px - dx, py - dy, px, py, 3, c1);
                art.drawLine(px, py, px + dx, py + dy, 3, c2);
            } else if (brushMode == 3) {
                art.fillCircle(px, py, 4, rgba(45, 35, 25));
                if (s == 0 && strokeStep % 3 == 0) {
                    float sproutAng = tiltRad - 1.57f + ((strokeStep % 2 == 0) ? 0.5f : -0.5f);
                    float sproutLen = 22.0f + pressure * 48.0f;
                    drawBotanicalBranch(art, (float)px, (float)py, sproutLen, sproutAng, 4, rgba(55, 40, 30), col);
                }
            }
        }
    }

    void drawWaveFabricDemo(Canvas2D &c) {
        // Full pure black background for maximum monochrome ANIMATE contrast
        c.fillRect(VX, VY + 90, VW, VH - 90, rgba(0, 0, 0));

        int cx = VX + VW / 2;
        int cy = VY + VH / 2 + 60;
        float scaleX = 22.5f;
        float scaleY = 14.5f;

        auto project = [&](int gx, int gy, int &sx, int &sy) {
            float fx = (float)gx - (GRID_N - 1) * 0.5f;
            float fy = (float)gy - (GRID_N - 1) * 0.5f;
            float isoX = (fx - fy) * scaleX;
            float isoY = (fx + fy) * scaleY - waveH[gy][gx] * 1.45f;
            sx = cx + (int)isoX;
            sy = cy + (int)isoY;
        };

        // Back-to-front painter's order (gy from 0..GRID_N-2, gx from 0..GRID_N-2):
        // Fill surface quads in pure white (255,255,255) with crisp full-black mesh lines
        for (int gy = 0; gy < GRID_N - 1; gy++) {
            for (int gx = 0; gx < GRID_N - 1; gx++) {
                int x00, y00, x10, y10, x01, y01, x11, y11;
                project(gx, gy, x00, y00);
                project(gx + 1, gy, x10, y10);
                project(gx, gy + 1, x01, y01);
                project(gx + 1, gy + 1, x11, y11);

                c.fillTriangleDither(x00, y00, x10, y10, x11, y11, 255, 255, 255, VX + 4, VY + 94, VX + VW - 4, VY + VH - 4);
                c.fillTriangleDither(x00, y00, x11, y11, x01, y01, 255, 255, 255, VX + 4, VY + 94, VX + VW - 4, VY + VH - 4);

                c.drawLine(x00, y00, x10, y10, 2, rgba(0, 0, 0));
                c.drawLine(x00, y00, x01, y01, 2, rgba(0, 0, 0));
                if (gx == GRID_N - 2) c.drawLine(x10, y10, x11, y11, 2, rgba(0, 0, 0));
                if (gy == GRID_N - 2) c.drawLine(x01, y01, x11, y11, 2, rgba(0, 0, 0));
            }
        }
        c.drawTextCentered(VX, VY + VH - 54, VW, 40, "TOUCH OR DRAG MARKER TO DEFORM 3D SPACETIME FABRIC (PRESSURE = GRAVITY DEPTH)", 2, rgba(255, 255, 255), true);
    }

    void drawPolyhedraDemo(Canvas2D &c, const RawMarkerState &marker) {
        c.fillDitherGradientRect(VX, VY + 90, VW, VH - 90, rgba(18, 24, 38), rgba(40, 30, 55));

        int cx = VX + VW / 2;
        int cy = VY + (VH + 90) / 2;

        float lx = -0.45f + (float)marker.tiltX * 0.0003f;
        float ly = -0.60f + (float)marker.tiltY * 0.0003f;
        float lz = 0.75f;
        float lLen = sqrtf(lx * lx + ly * ly + lz * lz);
        lx /= lLen; ly /= lLen; lz /= lLen;

        auto rot3D = [&](Vec3 v) -> Vec3 {
            float cy1 = cosf(rotY), sy1 = sinf(rotY);
            float x1 = v.x * cy1 + v.z * sy1;
            float z1 = -v.x * sy1 + v.z * cy1;
            float cx1 = cosf(rotX), sx1 = sinf(rotX);
            float y2 = v.y * cx1 - z1 * sx1;
            float z2 = v.y * sx1 + z1 * cx1;
            return {x1, y2, z2};
        };

        if (polyShape == 1) {
            rotW += 0.045f;
            rotY += 0.025f * autoSpin;
            int sx[16], sy[16];
            for (int i = 0; i < 16; i++) {
                float x4 = (i & 1) ? 1.0f : -1.0f;
                float y4 = (i & 2) ? 1.0f : -1.0f;
                float z4 = (i & 4) ? 1.0f : -1.0f;
                float w4 = (i & 8) ? 1.0f : -1.0f;

                float cw = cosf(rotW), sw = sinf(rotW);
                float x4r = x4 * cw - w4 * sw;
                float w4r = x4 * sw + w4 * cw;

                float cz = cosf(rotW * 0.7f), sz4 = sinf(rotW * 0.7f);
                float z4r = z4 * cz - w4r * sz4;
                float w4f = z4 * sz4 + w4r * cz;

                float persp4 = 2.6f / (3.4f - w4f);
                Vec3 p3 = rot3D({x4r * persp4, y4 * persp4, z4r * persp4});
                float p2 = 540.0f / (4.5f - p3.z * 0.35f);
                sx[i] = cx + (int)(p3.x * p2);
                sy[i] = cy + (int)(p3.y * p2);
            }
            for (int i = 0; i < 16; i++) {
                for (int bit = 0; bit < 4; bit++) {
                    int j = i ^ (1 << bit);
                    if (i < j) {
                        uint32_t edgeCol = (bit == 3) ? rgba(255, 195, 35) :
                                           ((i & 8) ? rgba(50, 210, 255) : rgba(255, 65, 145));
                        c.drawLine(sx[i], sy[i], sx[j], sy[j], 5, edgeCol);
                    }
                }
            }
            for (int i = 0; i < 16; i++) {
                c.fillCircle(sx[i], sy[i], 11, (i & 8) ? rgba(50, 210, 255) : rgba(255, 220, 60));
                c.drawCircle(sx[i], sy[i], 11, 2, rgba(15, 15, 20));
            }
            c.drawTextCentered(VX, VY + VH - 54, VW, 40, "4D HYPERCUBE (TESSERACT) | DRAG TO ORBIT IN 3D, PRESS TO MORPH 4D W-PLANE", 2, rgba(240, 240, 255), true);
            return;
        }

        rotY += 0.035f * autoSpin;
        const int NU = 28, NV = 18;
        Vec3 verts[(NU + 1) * (NV + 1)];
        float twist = pointerDown ? (normPressure * 1.8f) : 0.0f;

        for (int u = 0; u <= NU; u++) {
            float fu = (float)u / (float)NU * 6.2831853f;
            for (int v = 0; v <= NV; v++) {
                float fv = (float)v / (float)NV * 6.2831853f;
                Vec3 pt;
                if (polyShape == 0) {
                    float R = 340.0f, r = 145.0f + 35.0f * sinf(fu * 3.0f + twist * 2.0f);
                    pt.x = (R + r * cosf(fv)) * cosf(fu);
                    pt.y = (R + r * cosf(fv)) * sinf(fu);
                    pt.z = r * sinf(fv);
                } else if (polyShape == 2) {
                    float th = (float)v / (float)NV * 3.1415926f;
                    float rad = 380.0f + 75.0f * sinf(5.0f * fu) * sinf(4.0f * th);
                    pt.x = rad * sinf(th) * cosf(fu);
                    pt.y = rad * sinf(th) * sinf(fu);
                    pt.z = rad * cosf(th);
                } else {
                    float r = 225.0f + 95.0f * cosf(3.0f * fu);
                    float cx0 = r * cosf(2.0f * fu);
                    float cy0 = r * sinf(2.0f * fu);
                    float cz0 = 150.0f * sinf(3.0f * fu);
                    float tube = 72.0f;
                    pt.x = cx0 + tube * cosf(fv) * cosf(2.0f * fu);
                    pt.y = cy0 + tube * cosf(fv) * sinf(2.0f * fu);
                    pt.z = cz0 + tube * sinf(fv);
                }
                verts[u * (NV + 1) + v] = rot3D(pt);
            }
        }

        struct QuadFace {
            int x0, y0, x1, y1, x2, y2;
            float z;
            uint8_t r, g, b;
        };
        std::vector<QuadFace> tris;
        tris.reserve(NU * NV * 2);

        for (int u = 0; u < NU; u++) {
            for (int v = 0; v < NV; v++) {
                Vec3 p00 = verts[u * (NV + 1) + v];
                Vec3 p10 = verts[(u + 1) * (NV + 1) + v];
                Vec3 p11 = verts[(u + 1) * (NV + 1) + (v + 1)];
                Vec3 p01 = verts[u * (NV + 1) + (v + 1)];

                Vec3 e1 = {p10.x - p00.x, p10.y - p00.y, p10.z - p00.z};
                Vec3 e2 = {p01.x - p00.x, p01.y - p00.y, p01.z - p00.z};
                Vec3 n = {
                    e1.y * e2.z - e1.z * e2.y,
                    e1.z * e2.x - e1.x * e2.z,
                    e1.x * e2.y - e1.y * e2.x
                };
                float nl = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
                if (nl > 1e-4f) { n.x /= nl; n.y /= nl; n.z /= nl; }

                float diff = std::max(0.15f, fabsf(n.x * lx + n.y * ly + n.z * lz));
                float spec = powf(std::max(0.0f, fabsf(n.z)), 14.0f) * 0.45f;
                uint32_t baseCol = hsvToRgba((float)((u * 360) / NU), 0.82f, std::min(1.0f, diff + spec));
                uint8_t cr = baseCol & 0xFF, cg = (baseCol >> 8) & 0xFF, cb = (baseCol >> 16) & 0xFF;

                auto proj = [&](const Vec3 &p, int &ox, int &oy) {
                    float s = 950.0f / (950.0f - p.z * 0.45f);
                    ox = cx + (int)(p.x * s);
                    oy = cy + (int)(p.y * s);
                };
                int x00, y00, x10, y10, x11, y11, x01, y01;
                proj(p00, x00, y00); proj(p10, x10, y10);
                proj(p11, x11, y11); proj(p01, x01, y01);
                float avgZ = (p00.z + p10.z + p11.z + p01.z) * 0.25f;

                tris.push_back({x00, y00, x10, y10, x11, y11, avgZ, cr, cg, cb});
                tris.push_back({x00, y00, x11, y11, x01, y01, avgZ, cr, cg, cb});
            }
        }
        std::sort(tris.begin(), tris.end(), [](const QuadFace &a, const QuadFace &b) {
            return a.z < b.z;
        });

        for (const auto &t : tris) {
            c.fillTriangleDither(t.x0, t.y0, t.x1, t.y1, t.x2, t.y2, t.r, t.g, t.b, VX + 4, VY + 94, VX + VW - 4, VY + VH - 4);
            c.drawLine(t.x0, t.y0, t.x1, t.y1, 1, rgba(20, 22, 30));
        }
        c.drawTextCentered(VX, VY + VH - 54, VW, 40, "DRAG TO ORBIT 3D MESH | TILT MARKER PLUS TO STEER DIRECTIONAL SUNLIGHT", 2, rgba(240, 240, 255), true);
    }

    void drawParticleVortexDemo(Canvas2D &c) {
        c.fillRect(VX, VY + 90, VW, VH - 90, rgba(14, 18, 26));
        for (int i = 0; i < NUM_PARTICLES; i++) {
            const Particle &p = particles[i];
            uint32_t col = hsvToRgba((float)p.hue * 1.4f, 0.88f, 0.96f);
            int ix = (int)p.x, iy = (int)p.y;
            int tx = (int)(p.x - p.vx * 2.2f), ty = (int)(p.y - p.vy * 2.2f);
            c.drawLine(tx, ty, ix, iy, 3, col);
            c.fillRect(ix - 2, iy - 2, 5, 5, rgba(255, 255, 255));
        }
        if (pointerDown && ptrX >= VX && ptrY >= VY + 90) {
            c.drawCircle(ptrX, ptrY, (int)(28 + normPressure * 45), 3, rgba(255, 220, 60));
        }
        c.drawTextCentered(VX, VY + VH - 54, VW, 40, "2,200 NEON PARTICLES | PEN TIP = SINGULARITY ATTRACTOR | ERASER = SUPERNOVA REPULSOR", 2, rgba(220, 235, 255), true);
    }

    void drawCalligrapherDemo(Canvas2D &c, const RawMarkerState &marker) {
        for (int y = 90; y < VH; y++) {
            uint32_t *dst = c.fb + (VY + y) * c.W + VX;
            const uint32_t *src = artCanvas.data() + y * VW;
            memcpy(dst, src, VW * sizeof(uint32_t));
        }

        // Top Live Stylus Telemetry HUD strip inside the studio canvas (y: VY+96..VY+200)
        int hudY = VY + 96;
        c.fillRoundRect(VX + 16, hudY, VW - 32, 102, 12, rgba(26, 32, 44), rgba(70, 85, 110), 2);
        char sBuf[128];
        int rawP = marker.rawPressure > 0 ? marker.rawPressure : (pointerDown ? (int)(normPressure * 4095) : 0);
        snprintf(sBuf, sizeof(sBuf), "PRESSURE: %4d/4095 | TILT:%+d,%+d | %s",
                 rawP, marker.tiltX / 100, marker.tiltY / 100, marker.toolRubber ? "ERASER" : "PEN NIB");
        c.drawText(VX + 32, hudY + 18, sBuf, 2, rgba(255, 255, 255), true, 640);

        // Pressure Bar Gauge (fits strictly on left side before swatches!)
        int pBarX = VX + 32, pBarY = hudY + 54, pBarW = 630, pBarH = 26;
        c.fillRoundRect(pBarX, pBarY, pBarW, pBarH, 6, rgba(55, 65, 85));
        int pFill = std::clamp((rawP * (pBarW - 4)) / 4095, 0, pBarW - 4);
        if (pFill > 0) c.fillRect(pBarX + 2, pBarY + 2, pFill, pBarH - 4, rgba(45, 210, 115));

        // 8 Color Swatch Selector Pills on the right of the HUD (x: VX+690..VX+1540)
        int swX0 = VX + 690;
        for (int i = 0; i < 8; i++) {
            btnColorSelect[i] = {swX0 + i * 106, hudY + 18, 94, 66};
            uint32_t swCol = getPaletteColor(i, 20);
            bool sel = (selectedColorIdx == i);
            c.fillRoundRect(btnColorSelect[i].x, btnColorSelect[i].y, btnColorSelect[i].w, btnColorSelect[i].h,
                            10, swCol, sel ? rgba(255, 255, 255) : rgba(20, 20, 25), sel ? 5 : 2);
            if (i == 7) {
                c.drawTextCentered(btnColorSelect[i].x, btnColorSelect[i].y, btnColorSelect[i].w, btnColorSelect[i].h, "PRISM", 2, rgba(255, 255, 255), true);
            }
        }
    }

    void drawPage(Canvas2D &c, const RawMarkerState &marker) {
        // Sub-Demo Selector Bar (y: 180..252)
        const char *demoTitles[4] = {
            "1. 3D WAVE FABRIC",
            "2. 3D/4D POLYHEDRA",
            "3. PARTICLE VORTEX",
            "4. BOTANICAL STUDIO"
        };
        int tabW = (VW - 36) / 4;
        for (int i = 0; i < 4; i++) {
            btnDemoSelect[i] = {VX + i * (tabW + 12), 180, tabW, 72};
            bool act = (activeDemo == i);
            c.fillRoundRect(btnDemoSelect[i].x, btnDemoSelect[i].y, btnDemoSelect[i].w, btnDemoSelect[i].h, 12,
                            act ? rgba(165, 35, 135) : rgba(235, 238, 244), rgba(30, 35, 45), 3);
            c.drawTextCentered(btnDemoSelect[i].x, btnDemoSelect[i].y, btnDemoSelect[i].w, btnDemoSelect[i].h,
                               demoTitles[i], 2, act ? rgba(255, 255, 255) : rgba(25, 30, 40), true);
        }

        // Action / Preset Sub-Toolbar inside the Studio Frame (y: VY..VY+90)
        c.fillRoundRect(VX, VY, VW, VH, 14, rgba(250, 250, 246), rgba(35, 40, 50), 3);
        c.fillRect(VX + 3, VY + 3, VW - 6, 86, rgba(232, 236, 242));

        const char *actLabels[4][4] = {
            {"CENTER PULSE", "QUAD RIPPLE", "RING SOLITON", "FLAT RESET"},
            {"TORUS MESH", "4D HYPERCUBE", "HARMONIC SPHERE", "TREFOIL KNOT"},
            {"ORBIT ATTRACT", "DUAL GALAXY", "SHOCKWAVE", "RESEED SWARM"},
            {"FOUNTAIN NIB", "CHISEL TILT", "PRISM RIBBON", "BLOOM / CLEAR"}
        };
        int actW = (VW - 80) / 4;
        for (int i = 0; i < 4; i++) {
            btnAction[i] = {VX + 16 + i * (actW + 16), VY + 12, actW, 66};
            bool highlight = (activeDemo == 1 && polyShape == i) ||
                             (activeDemo == 2 && vortexMode == i) ||
                             (activeDemo == 3 && brushMode == i);
            c.fillRoundRect(btnAction[i].x, btnAction[i].y, btnAction[i].w, btnAction[i].h, 10,
                            highlight ? rgba(30, 100, 210) : rgba(255, 255, 255), rgba(45, 55, 70), 2);
            c.drawTextCentered(btnAction[i].x, btnAction[i].y, btnAction[i].w, btnAction[i].h,
                               actLabels[activeDemo][i], 2, highlight ? rgba(255, 255, 255) : rgba(25, 30, 40), true);
        }

        if (activeDemo == 0) drawWaveFabricDemo(c);
        else if (activeDemo == 1) drawPolyhedraDemo(c, marker);
        else if (activeDemo == 2) drawParticleVortexDemo(c);
        else drawCalligrapherDemo(c, marker);

        c.drawRect(VX, VY, VW, VH, 3, rgba(30, 35, 45));
    }

    bool handleTap(int x, int y) {
        for (int i = 0; i < 4; i++) {
            if (btnDemoSelect[i].contains(x, y)) {
                activeDemo = i;
                return true;
            }
        }
        for (int i = 0; i < 4; i++) {
            if (btnAction[i].contains(x, y)) {
                if (activeDemo == 0) {
                    if (i == 0) impulseWave(GRID_N * 0.5f, GRID_N * 0.5f, 145.0f, 5.5f);
                    else if (i == 1) {
                        impulseWave(GRID_N * 0.3f, GRID_N * 0.3f, 120.0f, 4.0f);
                        impulseWave(GRID_N * 0.7f, GRID_N * 0.3f, -120.0f, 4.0f);
                        impulseWave(GRID_N * 0.3f, GRID_N * 0.7f, -120.0f, 4.0f);
                        impulseWave(GRID_N * 0.7f, GRID_N * 0.7f, 120.0f, 4.0f);
                    } else if (i == 2) seedWaveShowcase();
                    else resetWaveGrid();
                } else if (activeDemo == 1) {
                    polyShape = i;
                } else if (activeDemo == 2) {
                    if (i < 3) vortexMode = i;
                    else initParticles();
                } else if (activeDemo == 3) {
                    if (i < 3) brushMode = i;
                    else {
                        if (brushMode != 3) brushMode = 3;
                        else initArtCanvas();
                    }
                }
                return true;
            }
        }
        if (activeDemo == 3) {
            for (int i = 0; i < 8; i++) {
                if (btnColorSelect[i].contains(x, y)) {
                    selectedColorIdx = i;
                    return true;
                }
            }
        }
        return false;
    }
};
