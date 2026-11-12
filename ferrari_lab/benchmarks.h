#pragma once
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
#include <thread>
#include <vector>
#include <atomic>
#if defined(__aarch64__)
#include <arm_neon.h>
#endif
#include "gfx2d.h"

static inline double nowSec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

struct BenchmarkMetric {
    const char *title;
    const char *subtitle;
    char primaryText[64];
    char secondaryText[64];
    float normalizedScore; // 0..100+
    bool completed;
    uint32_t accentColor;
};

class BenchmarkSuite {
public:
    BenchmarkMetric metrics[5];
    int compositeScore = 0;
    bool runningAll = false;
    int currentStep = -1;
    int visualMode = 0; // 0=Color Gamut & Siemens Star, 1=4-Core Mandelbrot, 2=Vector/SDF Stress
    int fullscreenPattern = 0; // 0 = Normal UI, 1..24 = Full-page 1620x2160 pattern view
    int previewScreenX = 926;
    int previewScreenY = 310;

    // Clean backing store & HUD state for Full-Screen Pattern Pen-Tip Color Inspector
    std::vector<uint32_t> fullscreenCleanBuf;
    bool inspectorVisible = false;
    int inspRectX = 0, inspRectY = 0, inspRectW = 0, inspRectH = 0;
    int lastInspX = -1, lastInspY = -1;
    double lastInspTime = 0.0;

    // Larger 654 x 836 preview canvas buffer for full-height portrait layout
    static const int PREV_W = 654;
    static const int PREV_H = 836;
    std::vector<uint32_t> previewBuf;

    // Telemetry history ring buffer (120 samples)
    static const int HIST_LEN = 120;
    float histCpuLoad[HIST_LEN] = {0};
    float histCpuTemp[HIST_LEN] = {0};
    float histPowerMw[HIST_LEN] = {0};
    int histHead = 0;

    UIButton btnRunAll;
    UIButton btnRunSingle[5];
    UIButton btnVisMode[3];

    BenchmarkSuite() : previewBuf(PREV_W * PREV_H, rgba(245, 245, 240)) {
        initMetrics();
        renderGamutAndSiemensChart();
    }

    void initMetrics() {
        metrics[0] = {"1. CPU SINGLE-CORE (CORTEX-A53)", "64-bit Prime Sieve + ChaCha20 ALU Mix", "Not Run", "Tap RUN to measure KPrimes/s", 0.0f, false, rgba(30, 100, 210)};
        metrics[1] = {"2. CPU 4-CORE SMP PARALLEL", "4x POSIX Threads Deep Fractal Kernel", "Not Run", "Tap RUN to test 4-core scaling", 0.0f, false, rgba(180, 40, 140)};
        metrics[2] = {"3. NEON SIMD & LPDDR4 RAM", "128-bit float32x4 FMA + 16MB Stream", "Not Run", "Tap RUN to test GB/s & GFLOP/s", 0.0f, false, rgba(20, 145, 90)};
        metrics[3] = {"4. 2D RASTER & GALLERY 3 DITHER", "Bayer 8x8 Color Quantizer + SDF Vectors", "Not Run", "Tap RUN to test pixel throughput", 0.0f, false, rgba(215, 110, 20)};
        metrics[4] = {"5. CUMULUS FPGA & E-INK REFRESH", "Partial UFAST Region Hz + Full Frame IPC", "Not Run", "Tap RUN to benchmark display link", 0.0f, false, rgba(190, 40, 45)};
    }

    void pushTelemetry(const HardwareTelemetry &hw) {
        histCpuLoad[histHead] = hw.totalCpuLoadPct;
        histCpuTemp[histHead] = hw.cpuTempC;
        histPowerMw[histHead] = fabsf(hw.battPowerMw);
        histHead = (histHead + 1) % HIST_LEN;
    }

    void recomputeTotalScore() {
        float sum = 0.0f;
        int count = 0;
        for (int i = 0; i < 5; i++) {
            if (metrics[i].completed) {
                sum += metrics[i].normalizedScore;
                count++;
            }
        }
        compositeScore = (count > 0) ? (int)(sum * 20.0f / (float)count) : 0;
    }

    // Visual Pattern 0: E Ink Gallery 3 Color Gamut, 16-Step Grayscale, Siemens Star, 3D Sphere & Optical Resolution Grilles
    void renderGamutAndSiemensChart() {
        visualMode = 0;
        for (int y = 0; y < PREV_H; y++) {
            for (int x = 0; x < PREV_W; x++) {
                previewBuf[y * PREV_W + x] = rgba(250, 250, 246);
            }
        }
        // 1. Top section: 8 Primary & Secondary E Ink Gallery 3 Pigment Swatches (y: 16..96)
        const uint32_t swatches[8] = {
            rgba(220, 35, 35),   // Red
            rgba(235, 135, 25),  // Orange
            rgba(235, 210, 25),  // Yellow
            rgba(35, 165, 65),   // Green
            rgba(25, 165, 210),  // Cyan
            rgba(35, 75, 210),   // Blue
            rgba(185, 40, 165),  // Magenta
            rgba(25, 25, 25)     // Carbon Black
        };
        int swW = (PREV_W - 32) / 8;
        for (int i = 0; i < 8; i++) {
            int sx = 16 + i * swW;
            for (int y = 16; y < 96; y++) {
                for (int x = sx; x < sx + swW - 4; x++) {
                    uint32_t c = swatches[i];
                    previewBuf[y * PREV_W + x] = ditherGallery3(x, y, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
                }
            }
        }
        // 2. Continuous HSV Dithered Spectrum Bar (y: 108..166)
        for (int y = 108; y < 166; y++) {
            float sat = 1.0f - 0.45f * (float)(y - 108) / 58.0f;
            for (int x = 16; x < PREV_W - 16; x++) {
                float hue = 360.0f * (float)(x - 16) / (float)(PREV_W - 32);
                uint32_t c = hsvToRgba(hue, sat, 0.95f);
                previewBuf[y * PREV_W + x] = ditherGallery3(x, y, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
            }
        }
        // 3. 16-Step Grayscale Linearity Ramp (y: 176..220)
        int gw = (PREV_W - 32) / 16;
        for (int i = 0; i < 16; i++) {
            uint8_t v = (uint8_t)((i * 255) / 15);
            int gx = 16 + i * gw;
            for (int y = 176; y < 220; y++) {
                for (int x = gx; x < gx + gw - 2; x++) {
                    previewBuf[y * PREV_W + x] = ditherGallery3(x, y, v, v, v);
                }
            }
        }
        // 4. Middle Left: High-Resolution 36-Spoke Siemens Star Optical Target (y: 236..536)
        int cx = 176, cy = 386, rad = 148;
        for (int y = cy - rad; y <= cy + rad; y++) {
            for (int x = cx - rad; x <= cx + rad; x++) {
                int dx = x - cx, dy = y - cy;
                float r = sqrtf((float)(dx * dx + dy * dy));
                if (r <= rad) {
                    float ang = atan2f((float)dy, (float)dx);
                    float v = sinf(ang * 24.0f);
                    uint32_t col = (v >= 0.0f) ? rgba(20, 20, 25) : rgba(248, 248, 244);
                    if (r < 6.0f) col = rgba(210, 40, 40);
                    previewBuf[y * PREV_W + x] = col;
                }
            }
        }
        // 5. Middle Right: Bayer Ordered Dither 3D Sphere Shading Target (y: 236..536)
        int scx = 482, scy = 386, srad = 144;
        for (int y = scy - srad; y <= scy + srad; y++) {
            for (int x = scx - srad; x <= scx + srad; x++) {
                float nx = (float)(x - scx) / (float)srad;
                float ny = (float)(y - scy) / (float)srad;
                float r2 = nx * nx + ny * ny;
                if (r2 <= 1.0f) {
                    float nz = sqrtf(1.0f - r2);
                    float diff = std::max(0.05f, -0.45f * nx - 0.55f * ny + 0.70f * nz);
                    float spec = powf(std::max(0.0f, -0.25f * nx - 0.35f * ny + 0.90f * nz), 24.0f);
                    int r = std::clamp((int)(40 + 185 * diff + 220 * spec), 0, 255);
                    int g = std::clamp((int)(65 + 120 * diff + 220 * spec), 0, 255);
                    int b = std::clamp((int)(160 + 85 * diff + 220 * spec), 0, 255);
                    previewBuf[y * PREV_W + x] = ditherGallery3(x, y, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                }
            }
        }
        // 6. Bottom Section (y: 554..820): Sub-Millimeter Line-Pair Grilles (1px..4px) & Gallery 3 Subtractive Mix Matrix
        Canvas2D prev(previewBuf.data(), PREV_W, PREV_H);
        prev.fillRoundRect(16, 554, PREV_W - 32, 266, 10, rgba(242, 244, 240), rgba(60, 65, 75), 2);
        prev.drawText(30, 568, "LINE-PAIR GRILLES (1-4PX) & CMY DITHER", 2, rgba(25, 30, 40), true, PREV_W - 60);

        // 4 Vertical & Horizontal Ronchi Rulings (1px, 2px, 3px, 4px pitch)
        for (int p = 1; p <= 4; p++) {
            int bx = 30 + (p - 1) * 150;
            int by = 602;
            int bw = 136, bh = 96;
            for (int y = by; y < by + bh; y++) {
                for (int x = bx; x < bx + bw; x++) {
                    bool half = (x - bx) < bw / 2;
                    int v = half ? (((x - bx) / p) & 1) : (((y - by) / p) & 1);
                    previewBuf[y * PREV_W + x] = v ? rgba(18, 20, 26) : rgba(250, 250, 248);
                }
            }
            prev.drawRect(bx, by, bw, bh, 2, rgba(40, 45, 55));
        }

        // Bottom row: 2D Color Barycentric Gamut & Checkerboard Micro-Contrast Strips (y: 712..804)
        for (int y = 712; y < 804; y++) {
            float ty = (float)(y - 712) / 92.0f;
            for (int x = 30; x < 420; x++) {
                float tx = (float)(x - 30) / 390.0f;
                uint8_t r = (uint8_t)((1.0f - tx) * 240.0f + tx * 30.0f);
                uint8_t g = (uint8_t)(ty * 225.0f + (1.0f - ty) * 35.0f);
                uint8_t b = (uint8_t)(tx * 240.0f + (1.0f - ty) * 60.0f);
                previewBuf[y * PREV_W + x] = ditherGallery3(x, y, r, g, b);
            }
            for (int x = 436; x < PREV_W - 30; x++) {
                int cell = ((x - 436) / 6 + (y - 712) / 6) & 1;
                previewBuf[y * PREV_W + x] = cell ? rgba(18, 22, 28) : rgba(250, 250, 246);
            }
        }
        prev.drawRect(30, 712, 390, 92, 2, rgba(40, 45, 55));
        prev.drawRect(436, 712, PREV_W - 466, 92, 2, rgba(40, 45, 55));
    }

    // Benchmark 1: Single-Core Integer, Branch & Crypto Mixing
    static double runSingleCoreWorkload(int iterations) {
        double t0 = nowSec();
        uint64_t state0 = 0x736f6d6570736575ULL;
        uint64_t state1 = 0x646f72616e646f6dULL;
        uint64_t primes = 0;
        for (int n = 3; n < iterations; n += 2) {
            bool isP = true;
            for (int d = 3; d * d <= n; d += 2) {
                if (n % d == 0) { isP = false; break; }
            }
            if (isP) {
                primes++;
                state0 ^= (uint64_t)n * 0x9E3779B97F4A7C15ULL;
                state1 += (state0 << 13) | (state0 >> 51);
                state0 = (state0 ^ state1) * 0xBF58476D1CE4E5B9ULL;
            }
        }
        if (state0 == 1 && primes == 0) printf("%llu", (unsigned long long)state1);
        return nowSec() - t0;
    }

    void runBenchmark1() {
        int limit = 240000;
        double dt = runSingleCoreWorkload(limit);
        double mops = (limit / 1000.0) / std::max(0.001, dt);
        snprintf(metrics[0].primaryText, sizeof(metrics[0].primaryText), "%.1f KPrimes/s (%.0f ms)", mops, dt * 1000.0);
        snprintf(metrics[0].secondaryText, sizeof(metrics[0].secondaryText), "1x Cortex-A53 @ 1800 MHz | Crypto+ALU");
        metrics[0].normalizedScore = std::clamp((float)(mops / 8.5), 10.0f, 150.0f);
        metrics[0].completed = true;
        recomputeTotalScore();
    }

    // Benchmark 2: 4-Core SMP Parallel Mandelbrot + Orbit Trap Renderer
    void runBenchmark2() {
        visualMode = 1;
        const int maxIter = 256;
        double tSingle0 = nowSec();
        volatile uint64_t dummy = 0;
        for (int y = 0; y < PREV_H; y += 4) {
            float ci = ((float)y / PREV_H - 0.5f) * 2.2f;
            for (int x = 0; x < PREV_W; x++) {
                float cr = ((float)x / PREV_W - 0.72f) * 2.4f;
                float zr = 0.0f, zi = 0.0f;
                int it = 0;
                while (zr * zr + zi * zi <= 4.0f && it < maxIter) {
                    float tr = zr * zr - zi * zi + cr;
                    zi = 2.0f * zr * zi + ci;
                    zr = tr;
                    it++;
                }
                dummy += it;
            }
        }
        double singleEst = (nowSec() - tSingle0) * 4.0;

        double tMulti0 = nowSec();
        const int passes = 4;
        std::atomic<int> nextRow{0};
        for (int p = 0; p < passes; p++) {
            nextRow.store(0);
            auto worker = [&]() {
                while (true) {
                    int y = nextRow.fetch_add(1);
                    if (y >= PREV_H) break;
                    float ci = ((float)y / PREV_H - 0.5f) * 2.2f;
                    for (int x = 0; x < PREV_W; x++) {
                        float cr = ((float)x / PREV_W - 0.72f) * 2.4f;
                        float zr = 0.0f, zi = 0.0f;
                        int it = 0;
                        while (zr * zr + zi * zi <= 4.0f && it < maxIter) {
                            float tr = zr * zr - zi * zi + cr;
                            zi = 2.0f * zr * zi + ci;
                            zr = tr;
                            it++;
                        }
                        if (p == passes - 1) {
                            if (it == maxIter) {
                                previewBuf[y * PREV_W + x] = rgba(18, 20, 28);
                            } else {
                                float t = (float)it / (float)maxIter;
                                uint32_t c = hsvToRgba(210.0f + t * 340.0f, 0.88f, std::min(1.0f, 0.35f + sqrtf(t) * 0.9f));
                                previewBuf[y * PREV_W + x] = ditherGallery3(x, y, c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF);
                            }
                        }
                    }
                }
            };
            std::thread t0(worker), t1(worker), t2(worker), t3(worker);
            t0.join(); t1.join(); t2.join(); t3.join();
        }
        double multiPerFrame = (nowSec() - tMulti0) / (double)passes;
        double scaling = std::clamp(singleEst / std::max(0.0001, multiPerFrame), 1.0, 4.0);
        double mpixPerSec = ((double)(PREV_W * PREV_H) / 1e6) / std::max(0.0001, multiPerFrame);

        snprintf(metrics[1].primaryText, sizeof(metrics[1].primaryText), "%.2f MPix/s (%.2fx SMP)", mpixPerSec, scaling);
        snprintf(metrics[1].secondaryText, sizeof(metrics[1].secondaryText), "4T: %.1f ms/frame vs 1T: %.1f ms", multiPerFrame * 1000.0, singleEst * 1000.0);
        metrics[1].normalizedScore = std::clamp((float)(mpixPerSec * 28.0), 10.0f, 150.0f);
        metrics[1].completed = true;
        recomputeTotalScore();
    }

    // Benchmark 3: ARMv8 NEON 128-bit SIMD & LPDDR4 Memory Stream Bandwidth
    void runBenchmark3() {
        const size_t N = 4 * 1024 * 1024; // 16 MB float array
        std::vector<float> a(N, 1.25f), b(N, 2.50f), c(N, 0.50f);

        double t0 = nowSec();
        int simdPasses = 12;
#if defined(__aarch64__)
        float32x4_t vScale = vdupq_n_f32(1.0001f);
        float32x4_t vBias  = vdupq_n_f32(0.0003f);
        for (int p = 0; p < simdPasses; p++) {
            float *pa = a.data();
            const float *pb = b.data();
            for (size_t i = 0; i < N; i += 16) {
                float32x4_t va0 = vld1q_f32(pa + i);
                float32x4_t vb0 = vld1q_f32(pb + i);
                float32x4_t va1 = vld1q_f32(pa + i + 4);
                float32x4_t vb1 = vld1q_f32(pb + i + 4);
                va0 = vmlaq_f32(vBias, va0, vmulq_f32(vb0, vScale));
                va1 = vmlaq_f32(vBias, va1, vmulq_f32(vb1, vScale));
                vst1q_f32(pa + i, va0);
                vst1q_f32(pa + i + 4, va1);
            }
        }
#else
        for (int p = 0; p < simdPasses; p++) {
            for (size_t i = 0; i < N; i++) a[i] = a[i] * b[i] * 1.0001f + 0.0003f;
        }
#endif
        double dtSimd = nowSec() - t0;

        double tMem0 = nowSec();
        int memPasses = 16;
        for (int p = 0; p < memPasses; p++) {
            memcpy(c.data(), a.data(), N * sizeof(float));
            a[p] += c[p];
        }
        double dtMem = nowSec() - tMem0;
        double gbPerSec = ((double)memPasses * 2.0 * (double)(N * sizeof(float)) / 1e9) / std::max(0.0001, dtMem);
        double gflops = ((double)simdPasses * (double)N * 2.0 / 1e9) / std::max(0.0001, dtSimd);

        snprintf(metrics[2].primaryText, sizeof(metrics[2].primaryText), "%.2f GB/s | %.2f GFLOP/s", gbPerSec, gflops);
        snprintf(metrics[2].secondaryText, sizeof(metrics[2].secondaryText), "ARMv8 128-bit NEON + LPDDR4 Stream");
        metrics[2].normalizedScore = std::clamp((float)(gbPerSec * 22.0), 10.0f, 150.0f);
        metrics[2].completed = true;
        recomputeTotalScore();
    }

    // Benchmark 4: 2D Rasterizer & Gallery 3 Bayer Dither Throughput
    void runBenchmark4() {
        visualMode = 2;
        Canvas2D prev(previewBuf.data(), PREV_W, PREV_H);
        double t0 = nowSec();
        int frames = 14;
        for (int f = 0; f < frames; f++) {
            prev.fillDitherGradientRect(0, 0, PREV_W, PREV_H, rgba(20, 35, 75), rgba(245, 225, 195));
            for (int i = 0; i < 85; i++) {
                int cx = (i * 97 + f * 23) % (PREV_W - 60) + 30;
                int cy = (i * 73 + f * 19) % (PREV_H - 60) + 30;
                int r = 14 + (i % 34);
                uint32_t col = hsvToRgba((float)((i * 29 + f * 15) % 360), 0.85f, 0.92f);
                prev.fillCircle(cx, cy, r, ditherGallery3(cx, cy, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF));
                prev.drawCircle(cx, cy, r, 2, rgba(20, 20, 25));
            }
        }
        double dt = nowSec() - t0;
        double fps = (double)frames / std::max(0.0001, dt);
        double mpixSec = fps * ((double)(PREV_W * PREV_H) / 1e6);

        snprintf(metrics[3].primaryText, sizeof(metrics[3].primaryText), "%.1f FPS (%.1f MPix/s)", fps, mpixSec);
        snprintf(metrics[3].secondaryText, sizeof(metrics[3].secondaryText), "Bayer 8x8 Dither + Vector SDF Engine");
        metrics[3].normalizedScore = std::clamp((float)(mpixSec * 1.35), 10.0f, 150.0f);
        metrics[3].completed = true;
        recomputeTotalScore();
    }

    // Benchmark 5: Physical Cumulus FPGA & E-Ink Waveform Refresh Benchmark
    void runBenchmark5(qtfb::Connection &conn, int previewScreenX, int previewScreenY) {
        renderGamutAndSiemensChart();
        int prevMode = conn.currentRefreshMode;
        conn.setRefreshMode(REFRESH_MODE_FAST);

        double t0 = nowSec();
        int partialBursts = 24;
        for (int i = 0; i < partialBursts; i++) {
            conn.updateRegion(previewScreenX, previewScreenY, PREV_W, PREV_H);
        }
        double dtPartial = nowSec() - t0;
        double hz = (double)partialBursts / std::max(0.001, dtPartial);

        double tFull0 = nowSec();
        conn.updateFull();
        double fullMs = (nowSec() - tFull0) * 1000.0;
        conn.setRefreshMode(prevMode);

        snprintf(metrics[4].primaryText, sizeof(metrics[4].primaryText), "%.0f Hz IPC | %.2f ms Full", hz, fullMs);
        snprintf(metrics[4].secondaryText, sizeof(metrics[4].secondaryText), "Cumulus Bridge MIPI-DSI + SHM Pipeline");
        metrics[4].normalizedScore = std::clamp(95.0f, 10.0f, 150.0f);
        metrics[4].completed = true;
        recomputeTotalScore();
    }

    // Map a tap inside the gallery preview box to a full-screen pattern id (1..24).
    // Returns true if a pattern was selected.
    bool handlePreviewTap(int screenX, int screenY) {
        int x = screenX - previewScreenX;
        int y = screenY - previewScreenY;
        if (x < 0 || y < 0 || x >= PREV_W || y >= PREV_H) return false;

        int sel = 0;
        if (visualMode == 1) {
            sel = 23;
        } else if (visualMode == 2) {
            sel = 24;
        } else {
            const int swW = (PREV_W - 32) / 8;
            auto inCircle = [&](int cx, int cy, int r) {
                int dx = x - cx, dy = y - cy;
                return dx * dx + dy * dy <= r * r;
            };
            if (y >= 8 && y < 102 && x >= 16 && x < 16 + 8 * swW) {
                sel = 1 + std::clamp((x - 16) / swW, 0, 7);            // Pigment swatches
            } else if (y >= 102 && y < 171 && x >= 16 && x < PREV_W - 16) {
                sel = 9;                                               // HSV spectrum
            } else if (y >= 171 && y < 228 && x >= 16 && x < PREV_W - 16) {
                sel = 10;                                              // 16-step grayscale
            } else if (inCircle(176, 386, 152)) {
                sel = 11;                                              // Siemens star
            } else if (inCircle(482, 386, 148)) {
                sel = 12;                                              // 3D sphere
            } else if (y >= 596 && y < 704) {
                for (int p = 1; p <= 4; p++) {                         // 1..4 px grilles
                    int bx = 30 + (p - 1) * 150;
                    if (x >= bx - 6 && x < bx + 142) {
                        bool verticalHalf = (x - bx) < 68;
                        sel = 13 + (p - 1) * 2 + (verticalHalf ? 0 : 1);
                        break;
                    }
                }
            } else if (y >= 706 && y < 812) {
                if (x >= 24 && x < 426) sel = 21;                      // Barycentric gamut
                else if (x >= 430 && x < PREV_W - 24) sel = 22;        // Micro-contrast checkerboard
            }
        }
        if (sel == 0) return false;
        fullscreenPattern = sel;
        return true;
    }

    // Fill the entire framebuffer (1620x2160) edge-to-edge with the selected gallery pattern
    void drawFullscreenPattern(Canvas2D &c) {
        const int W = c.W, H = c.H;
        uint32_t *fb = c.fb;
        const uint32_t white = rgba(255, 255, 255);
        const uint32_t black = rgba(0, 0, 0);
        const int pat = fullscreenPattern;

        if (pat >= 1 && pat <= 8) {
            const uint32_t swatches[8] = {
                rgba(220, 35, 35), rgba(235, 135, 25), rgba(235, 210, 25), rgba(35, 165, 65),
                rgba(25, 165, 210), rgba(35, 75, 210), rgba(185, 40, 165), rgba(25, 25, 25)
            };
            uint32_t col = swatches[pat - 1];
            uint8_t r = col & 0xFF, g = (col >> 8) & 0xFF, b = (col >> 16) & 0xFF;
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    fb[y * W + x] = ditherGallery3(x, y, r, g, b);
        } else if (pat == 9) {
            for (int y = 0; y < H; y++) {
                float sat = 1.0f - 0.45f * (float)y / (float)(H - 1);
                for (int x = 0; x < W; x++) {
                    uint32_t col = hsvToRgba(360.0f * (float)x / (float)W, sat, 0.95f);
                    fb[y * W + x] = ditherGallery3(x, y, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF);
                }
            }
        } else if (pat == 10) {
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    int step = std::min(15, (x * 16) / W);
                    uint8_t v = (uint8_t)((step * 255) / 15);
                    fb[y * W + x] = ditherGallery3(x, y, v, v, v);
                }
            }
        } else if (pat == 11) {
            const float cx = W * 0.5f, cy = H * 0.5f;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    float dx = (float)x - cx, dy = (float)y - cy;
                    float ang = atan2f(dy, dx);
                    uint32_t col = (sinf(ang * 24.0f) >= 0.0f) ? black : white;
                    if (dx * dx + dy * dy < 100.0f) col = rgba(210, 40, 40);
                    fb[y * W + x] = col;
                }
            }
        } else if (pat == 12) {
            const float scx = W * 0.5f, scy = H * 0.5f, srad = W * 0.47f;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    float nx = ((float)x - scx) / srad;
                    float ny = ((float)y - scy) / srad;
                    float r2 = nx * nx + ny * ny;
                    if (r2 <= 1.0f) {
                        float nz = sqrtf(1.0f - r2);
                        float diff = std::max(0.05f, -0.45f * nx - 0.55f * ny + 0.70f * nz);
                        float spec = powf(std::max(0.0f, -0.25f * nx - 0.35f * ny + 0.90f * nz), 24.0f);
                        int r = std::clamp((int)(40 + 185 * diff + 220 * spec), 0, 255);
                        int g = std::clamp((int)(65 + 120 * diff + 220 * spec), 0, 255);
                        int b = std::clamp((int)(160 + 85 * diff + 220 * spec), 0, 255);
                        fb[y * W + x] = ditherGallery3(x, y, (uint8_t)r, (uint8_t)g, (uint8_t)b);
                    } else {
                        fb[y * W + x] = rgba(250, 250, 246);
                    }
                }
            }
        } else if (pat >= 13 && pat <= 20) {
            int p = (pat - 13) / 2 + 1;           // pitch 1..4 px
            bool vertical = ((pat - 13) % 2) == 0;
            for (int y = 0; y < H; y++) {
                for (int x = 0; x < W; x++) {
                    int v = vertical ? ((x / p) & 1) : ((y / p) & 1);
                    fb[y * W + x] = v ? black : white;
                }
            }
        } else if (pat == 21) {
            for (int y = 0; y < H; y++) {
                float ty = (float)y / (float)(H - 1);
                for (int x = 0; x < W; x++) {
                    float tx = (float)x / (float)(W - 1);
                    uint8_t r = (uint8_t)((1.0f - tx) * 240.0f + tx * 30.0f);
                    uint8_t g = (uint8_t)(ty * 225.0f + (1.0f - ty) * 35.0f);
                    uint8_t b = (uint8_t)(tx * 240.0f + (1.0f - ty) * 60.0f);
                    fb[y * W + x] = ditherGallery3(x, y, r, g, b);
                }
            }
        } else if (pat == 22) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++)
                    fb[y * W + x] = ((x / 6 + y / 6) & 1) ? black : white;
        } else if (pat == 23) {
            // 4-thread full-page Mandelbrot (same palette/framing as the preview)
            const int maxIter = 256;
            std::atomic<int> nextRow{0};
            auto worker = [&]() {
                while (true) {
                    int y = nextRow.fetch_add(1);
                    if (y >= H) break;
                    float ci = ((float)y / H - 0.5f) * 2.2f;
                    for (int x = 0; x < W; x++) {
                        float cr = ((float)x / W - 0.72f) * 2.4f;
                        float zr = 0.0f, zi = 0.0f;
                        int it = 0;
                        while (zr * zr + zi * zi <= 4.0f && it < maxIter) {
                            float tr = zr * zr - zi * zi + cr;
                            zi = 2.0f * zr * zi + ci;
                            zr = tr;
                            it++;
                        }
                        if (it == maxIter) {
                            fb[y * W + x] = rgba(18, 20, 28);
                        } else {
                            float t = (float)it / (float)maxIter;
                            uint32_t col = hsvToRgba(210.0f + t * 340.0f, 0.88f, std::min(1.0f, 0.35f + sqrtf(t) * 0.9f));
                            fb[y * W + x] = ditherGallery3(x, y, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF);
                        }
                    }
                }
            };
            std::thread t0(worker), t1(worker), t2(worker), t3(worker);
            t0.join(); t1.join(); t2.join(); t3.join();
        } else if (pat == 24) {
            // Vector SDF + Bayer dither scene scaled to the full page (final frame of benchmark 4)
            const float sx = (float)W / (float)PREV_W, sy = (float)H / (float)PREV_H;
            const float sr = std::min(sx, sy);
            const int f = 13;
            c.fillDitherGradientRect(0, 0, W, H, rgba(20, 35, 75), rgba(245, 225, 195));
            for (int i = 0; i < 85; i++) {
                int cx = (int)((float)((i * 97 + f * 23) % (PREV_W - 60) + 30) * sx);
                int cy = (int)((float)((i * 73 + f * 19) % (PREV_H - 60) + 30) * sy);
                int r = (int)((float)(14 + (i % 34)) * sr);
                uint32_t col = hsvToRgba((float)((i * 29 + f * 15) % 360), 0.85f, 0.92f);
                c.fillCircle(cx, cy, r, ditherGallery3(cx, cy, col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF));
                c.drawCircle(cx, cy, r, 4, rgba(20, 20, 25));
            }
        }

        // Snapshot the clean full-page pattern so Pen-Tip Color Inspection can restore underlying pixels cleanly
        fullscreenCleanBuf.assign(c.fb, c.fb + W * H);
        inspectorVisible = false;
        lastInspX = -1;
        lastInspY = -1;
    }

    static inline const char *classifyGallery3Microcup(int r, int g, int b) {
        if (r >= 225 && g >= 225 && b >= 225) return "WHITE (W)";
        if (r <= 45  && g <= 45  && b <= 45)  return "BLACK (C+M+Y)";
        if (r >= 195 && g >= 175 && b <= 85)  return "YELLOW (Y)";
        if (r >= 180 && g <= 90  && b <= 90)  return "RED (M+Y)";
        if (r <= 90  && g >= 140 && b <= 110) return "GREEN (C+Y)";
        if (r <= 90  && g <= 115 && b >= 165) return "BLUE (C+M)";
        if (r <= 95  && g >= 145 && b >= 165) return "CYAN (C)";
        if (r >= 150 && g <= 95  && b >= 145) return "MAGENTA (M)";
        return "G3 MIXED";
    }

    // Live Pen-Tip Color Inspector in Full-Screen Gallery View (displays RGB888, Hex, 8x8 Bayer Avg, and E Ink Gallery 3 CMYW)
    void inspectFullscreenPixel(Canvas2D &c, qtfb::Connection &conn, int sx, int sy) {
        const int W = c.W, H = c.H;
        sx = std::clamp(sx, 0, W - 1);
        sy = std::clamp(sy, 0, H - 1);

        double now = nowSec();
        if (inspectorVisible && (now - lastInspTime) < 0.030 &&
            (abs(sx - lastInspX) + abs(sy - lastInspY)) < 4) {
            return;
        }
        if (fullscreenCleanBuf.size() != (size_t)(W * H)) return;

        int oldX0 = inspRectX, oldY0 = inspRectY, oldX1 = inspRectX + inspRectW, oldY1 = inspRectY + inspRectH;
        if (inspectorVisible && inspRectW > 0 && inspRectH > 0) {
            int rx0 = std::clamp(oldX0, 0, W), rx1 = std::clamp(oldX1, 0, W);
            int ry0 = std::clamp(oldY0, 0, H), ry1 = std::clamp(oldY1, 0, H);
            for (int y = ry0; y < ry1; y++) {
                memcpy(&c.fb[y * W + rx0], &fullscreenCleanBuf[y * W + rx0], (rx1 - rx0) * sizeof(uint32_t));
            }
        }

        // 1. Read exact pixel from clean full-screen pattern
        uint32_t exactCol = fullscreenCleanBuf[sy * W + sx];
        int r = exactCol & 0xFF;
        int g = (exactCol >> 8) & 0xFF;
        int b = (exactCol >> 16) & 0xFF;

        // 2. Compute 8x8 Bayer dither cell average around (sx, sy) for perceived Gallery 3 color
        int bx0 = std::clamp(sx - 3, 0, W - 8);
        int by0 = std::clamp(sy - 3, 0, H - 8);
        int sumR = 0, sumG = 0, sumB = 0;
        for (int dy = 0; dy < 8; dy++) {
            const uint32_t *row = &fullscreenCleanBuf[(by0 + dy) * W + bx0];
            for (int dx = 0; dx < 8; dx++) {
                uint32_t p = row[dx];
                sumR += (p & 0xFF);
                sumG += ((p >> 8) & 0xFF);
                sumB += ((p >> 16) & 0xFF);
            }
        }
        int avgR = (sumR + 32) >> 6;
        int avgG = (sumG + 32) >> 6;
        int avgB = (sumB + 32) >> 6;
        uint32_t avgCol = rgba((uint8_t)avgR, (uint8_t)avgG, (uint8_t)avgB);

        // 3. E Ink Gallery 3 ACeP 4-Particle Subtractive Pigment Space (Cyan, Magenta, Yellow, White)
        int cPct = ((255 - avgR) * 100 + 127) / 255;
        int mPct = ((255 - avgG) * 100 + 127) / 255;
        int yPct = ((255 - avgB) * 100 + 127) / 255;
        int wPct = (std::min({avgR, avgG, avgB}) * 100 + 127) / 255;
        const char *g3State = classifyGallery3Microcup(r, g, b);

        // 4. Draw high-contrast target reticle at (sx, sy)
        const int retR = 16;
        c.drawCircle(sx, sy, retR + 2, 2, rgba(255, 255, 255));
        c.drawCircle(sx, sy, retR,     2, rgba(15, 20, 30));
        c.fillRect(sx - retR - 6, sy - 1, 8, 3, rgba(15, 20, 30));
        c.fillRect(sx + retR - 1, sy - 1, 8, 3, rgba(15, 20, 30));
        c.fillRect(sx - 1, sy - retR - 6, 3, 8, rgba(15, 20, 30));
        c.fillRect(sx - 1, sy + retR - 1, 3, 8, rgba(15, 20, 30));

        // 5. Place floating HUD card near (sx, sy) without covering the pen tip
        const int cardW = 556;
        const int cardH = 166;
        int cardX = (sx + 38 + cardW <= W - 16) ? (sx + 38) : (sx - 38 - cardW);
        cardX = std::clamp(cardX, 16, W - cardW - 16);
        int cardY = std::clamp(sy - cardH / 2, 16, H - cardH - 16);

        c.fillRoundRect(cardX, cardY, cardW, cardH, 12, rgba(22, 28, 40), rgba(255, 255, 255), 3);

        // Left dual color swatches (Top: Exact Pixel, Bottom: 8x8 Dither Cell Avg)
        int swX = cardX + 16, swY1 = cardY + 16, swW = 72, swH1 = 64;
        c.fillRoundRect(swX, swY1, swW, swH1, 8, exactCol, rgba(255, 255, 255), 2);
        int swY2 = swY1 + swH1 + 8, swH2 = 62;
        c.fillRoundRect(swX, swY2, swW, swH2, 8, avgCol, rgba(200, 210, 225), 2);

        int tx = swX + swW + 16;
        int maxTW = cardW - (tx - cardX) - 14;
        char line1[64], line2[64], line3[64], line4[64];
        snprintf(line1, sizeof(line1), "XY:(%d,%d) %s", sx, sy, g3State);
        snprintf(line2, sizeof(line2), "PIXEL RGB: %3d,%3d,%3d (#%02X%02X%02X)", r, g, b, r, g, b);
        snprintf(line3, sizeof(line3), "8x8 AVG  : %3d,%3d,%3d (#%02X%02X%02X)", avgR, avgG, avgB, avgR, avgG, avgB);
        snprintf(line4, sizeof(line4), "G3 CMYW  : C:%d%% M:%d%% Y:%d%% W:%d%%", cPct, mPct, yPct, wPct);

        c.drawText(tx, cardY + 14,  line1, 2, rgba(255, 215, 55),  true, maxTW);
        c.drawText(tx, cardY + 44,  line2, 2, rgba(255, 255, 255), true, maxTW);
        c.drawText(tx, cardY + 74,  line3, 2, rgba(175, 220, 255), true, maxTW);
        c.drawText(tx, cardY + 104, line4, 2, rgba(80, 235, 145),  true, maxTW);
        c.drawText(tx, cardY + 134, "PEN: INSPECT | ERASER: EXIT", 2, rgba(210, 180, 255), true, maxTW);

        int newX0 = std::max(0, std::min(sx - retR - 8, cardX - 2));
        int newY0 = std::max(0, std::min(sy - retR - 8, cardY - 2));
        int newX1 = std::min(W, std::max(sx + retR + 9, cardX + cardW + 2));
        int newY1 = std::min(H, std::max(sy + retR + 9, cardY + cardH + 2));

        int ux0 = inspectorVisible ? std::min(oldX0, newX0) : newX0;
        int uy0 = inspectorVisible ? std::min(oldY0, newY0) : newY0;
        int ux1 = inspectorVisible ? std::max(oldX1, newX1) : newX1;
        int uy1 = inspectorVisible ? std::max(oldY1, newY1) : newY1;

        inspRectX = newX0;
        inspRectY = newY0;
        inspRectW = newX1 - newX0;
        inspRectH = newY1 - newY0;
        inspectorVisible = true;
        lastInspX = sx;
        lastInspY = sy;
        lastInspTime = now;

        conn.updateRegion(ux0, uy0, ux1 - ux0, uy1 - uy0);
    }

    void drawPage(Canvas2D &c, const HardwareTelemetry &hw, int &outPrevX, int &outPrevY) {
        const int topY = 180;
        // Left Column (x: 24..892, width: 868, y: 180..2140)
        int leftX = 24, leftW = 868;

        // 1. Suite Banner Card (y: 180..316, h = 136)
        c.fillRoundRect(leftX, topY, leftW, 136, 14, rgba(22, 30, 46), rgba(12, 16, 26), 3);
        int bannerTextMaxW = leftW - 310;
        c.drawText(leftX + 24, topY + 20, "FERRARI BENCHMARK SUITE", 3, rgba(255, 255, 255), true, bannerTextMaxW);
        c.drawText(leftX + 24, topY + 58, "i.MX8M Mini (4x Cortex-A53) + Cumulus FPGA", 2, rgba(185, 210, 245), false, bannerTextMaxW);
        char scoreBuf[64];
        snprintf(scoreBuf, sizeof(scoreBuf), "COMPOSITE SCORE: %d PTS", compositeScore);
        c.drawText(leftX + 24, topY + 90, scoreBuf, 3, rgba(255, 215, 60), true, bannerTextMaxW);

        btnRunAll = {leftX + leftW - 274, topY + 20, 252, 96};
        c.fillRoundRect(btnRunAll.x, btnRunAll.y, btnRunAll.w, btnRunAll.h, 12,
                        runningAll ? rgba(220, 120, 20) : rgba(32, 152, 72), rgba(255, 255, 255), 3);
        c.drawTextCentered(btnRunAll.x, btnRunAll.y, btnRunAll.w, btnRunAll.h,
                           runningAll ? "RUNNING..." : "RUN ALL 5", 3, rgba(255, 255, 255), true);

        // 2. Five Individual Benchmark Cards (y: 332..1598, each cardH = 242, gap = 14)
        int cardY = topY + 152;
        int cardH = 242;
        int textMaxW = leftW - 196;
        for (int i = 0; i < 5; i++) {
            const BenchmarkMetric &m = metrics[i];
            c.fillRoundRect(leftX, cardY, leftW, cardH, 14, rgba(252, 252, 248), rgba(45, 50, 60), 3);
            c.fillRoundRect(leftX + 10, cardY + 14, 14, cardH - 28, 6, m.accentColor);

            c.drawText(leftX + 36, cardY + 18, m.title, 3, rgba(20, 24, 32), true, textMaxW);
            c.drawText(leftX + 36, cardY + 56, m.subtitle, 2, rgba(85, 92, 105), false, textMaxW);

            c.drawText(leftX + 36, cardY + 92, m.primaryText, 3, m.completed ? m.accentColor : rgba(110, 110, 115), true, textMaxW);
            c.drawText(leftX + 36, cardY + 132, m.secondaryText, 2, rgba(55, 62, 72), false, textMaxW);

            // Progress / Score Bar
            int barX = leftX + 36, barY = cardY + 178, barW = leftW - 196, barH = 42;
            c.fillRoundRect(barX, barY, barW, barH, 8, rgba(228, 230, 225), rgba(140, 145, 150), 2);
            if (m.completed) {
                int fillW = std::clamp((int)((m.normalizedScore / 120.0f) * (barW - 8)), 16, barW - 8);
                c.fillDitherGradientRect(barX + 4, barY + 4, fillW, barH - 8, m.accentColor, rgba(30, 35, 45));
            }

            // Individual [RUN] button on right
            btnRunSingle[i] = {leftX + leftW - 144, cardY + 24, 124, cardH - 48};
            c.fillRoundRect(btnRunSingle[i].x, btnRunSingle[i].y, btnRunSingle[i].w, btnRunSingle[i].h, 10,
                            rgba(235, 240, 248), m.accentColor, 3);
            c.drawTextCentered(btnRunSingle[i].x, btnRunSingle[i].y, btnRunSingle[i].w, btnRunSingle[i].h,
                               "RUN", 3, rgba(20, 25, 35), true);

            cardY += cardH + 14;
        }

        // 3. Bottom-Left Card (y: 1612..2140, h = 528): SMP 4-Core Utilization & Display Subsystem Matrix
        int smpY = cardY;
        int smpH = 2140 - smpY;
        c.fillRoundRect(leftX, smpY, leftW, smpH, 14, rgba(252, 252, 248), rgba(40, 45, 55), 3);
        c.drawText(leftX + 24, smpY + 18, "SMP 4-CORE LOAD & SUBSYSTEM MATRIX", 3, rgba(20, 24, 32), true, leftW - 48);

        // 4 CPU Core load bars + LPDDR4 RAM bar + Battery/Frontlight bar
        const uint32_t coreCols[4] = {
            rgba(30, 105, 215), rgba(20, 150, 95), rgba(215, 115, 20), rgba(175, 40, 145)
        };
        int rowY = smpY + 62;
        for (int core = 0; core < 4; core++) {
            char cLbl[64];
            snprintf(cLbl, sizeof(cLbl), "CORE %d (%d MHz)", core, hw.cpuFreqMhz[core]);
            c.drawText(leftX + 24, rowY + 8, cLbl, 2, rgba(30, 35, 45), true, 245);

            int bx = leftX + 280, bw = leftW - 415, bh = 34;
            c.fillRoundRect(bx, rowY, bw, bh, 7, rgba(226, 230, 235), rgba(140, 148, 160), 2);
            int fw = std::clamp((int)((hw.cpuLoadPct[core] / 100.0f) * (bw - 6)), 0, bw - 6);
            if (fw > 0) {
                c.fillDitherGradientRect(bx + 3, rowY + 3, fw, bh - 6, coreCols[core], rgba(25, 30, 42));
            }
            char pctLbl[32];
            snprintf(pctLbl, sizeof(pctLbl), "%3.0f%%", hw.cpuLoadPct[core]);
            c.drawText(bx + bw + 14, rowY + 8, pctLbl, 2, coreCols[core], true, 100);
            rowY += 46;
        }

        // Memory Utilization Bar
        {
            float memPct = (hw.memTotalMb > 0) ? (100.0f * hw.memUsedMb / hw.memTotalMb) : 35.0f;
            c.drawText(leftX + 24, rowY + 8, "LPDDR4 MEMORY", 2, rgba(30, 35, 45), true, 245);
            int bx = leftX + 280, bw = leftW - 415, bh = 34;
            c.fillRoundRect(bx, rowY, bw, bh, 7, rgba(226, 230, 235), rgba(140, 148, 160), 2);
            int fw = std::clamp((int)((memPct / 100.0f) * (bw - 6)), 4, bw - 6);
            c.fillDitherGradientRect(bx + 3, rowY + 3, fw, bh - 6, rgba(35, 130, 185), rgba(25, 40, 65));
            char mLbl[32];
            snprintf(mLbl, sizeof(mLbl), "%dMB", hw.memUsedMb);
            c.drawText(bx + bw + 14, rowY + 8, mLbl, 2, rgba(35, 100, 160), true, 110);
            rowY += 52;
        }

        // Cumulus FPGA E-Ink Waveform Reference Table at the bottom of the left card
        int tblX = leftX + 20, tblY = rowY, tblW = leftW - 40, tblH = smpY + smpH - rowY - 18;
        c.fillRoundRect(tblX, tblY, tblW, tblH, 10, rgba(236, 240, 246), rgba(80, 90, 105), 2);
        c.drawText(tblX + 16, tblY + 14, "OS 3.29 APPLOAD / EPSCREENMODE WAVEFORM SPECS:", 2, rgba(20, 28, 42), true, tblW - 32);
        c.drawText(tblX + 16, tblY + 46, "UFAST   (0): Native Adaptive Full-Color (Lowest Latency)", 2, rgba(35, 95, 200), false, tblW - 32);
        c.drawText(tblX + 16, tblY + 76, "ANIMATE (2): EPScreenMode.Animation (Fast Binary B&W)", 2, rgba(20, 135, 80), false, tblW - 32);
        c.drawText(tblX + 16, tblY + 106, "CONTENT (3): EPScreenMode.Content (Slow High-Detail Settle)", 2, rgba(165, 35, 135), false, tblW - 32);
        c.drawText(tblX + 16, tblY + 136, "PANEL: 1620x2160 (229 PPI) | DIGITIZER: Elan SPI 240Hz", 2, rgba(60, 65, 75), true, tblW - 32);

        // Right Column (x: 910..1596, width: 686, y: 180..2140)
        int rightX = 910, rightW = 686;

        // 1. Top Right Card (y: 180..1168, h = 988): Visual Benchmark & Gamut Target
        int visH = 988;
        c.fillRoundRect(rightX, topY, rightW, visH, 14, rgba(252, 252, 248), rgba(40, 45, 55), 3);
        c.drawText(rightX + 20, topY + 18, "GALLERY 3 VISUAL & GAMUT TARGET", 3, rgba(20, 24, 32), true, rightW - 40);

        const char *vLabels[3] = {"GAMUT & STAR", "4T MANDELBROT", "VECTOR DITHER"};
        int vBtnW = (rightW - 52) / 3;
        for (int v = 0; v < 3; v++) {
            btnVisMode[v] = {rightX + 16 + v * (vBtnW + 10), topY + 58, vBtnW, 56};
            bool active = (visualMode == v);
            c.fillRoundRect(btnVisMode[v].x, btnVisMode[v].y, btnVisMode[v].w, btnVisMode[v].h, 10,
                            active ? rgba(30, 95, 200) : rgba(235, 238, 242), rgba(40, 45, 55), 2);
            c.drawTextCentered(btnVisMode[v].x, btnVisMode[v].y, btnVisMode[v].w, btnVisMode[v].h,
                               vLabels[v], 2, active ? rgba(255, 255, 255) : rgba(25, 30, 40), true);
        }

        // Blit 654x836 previewBuf into right card
        int px0 = rightX + 16, py0 = topY + 130;
        outPrevX = px0;
        outPrevY = py0;
        previewScreenX = px0;
        previewScreenY = py0;
        for (int y = 0; y < PREV_H; y++) {
            uint32_t *dst = c.fb + (py0 + y) * c.W + px0;
            const uint32_t *src = previewBuf.data() + y * PREV_W;
            memcpy(dst, src, PREV_W * sizeof(uint32_t));
        }
        c.drawRect(px0, py0, PREV_W, PREV_H, 3, rgba(30, 35, 45));
        c.drawText(rightX + 20, py0 + PREV_H + 4, "TAP: FULL SCREEN | PEN: RGB | ERASER: EXIT", 2, rgba(85, 92, 105), true, rightW - 40);

        // 2. Bottom Right Card (y: 1184..2140, h = 956): Live Hardware Telemetry & Scrolling Oscilloscope
        int teleY = topY + visH + 16;
        int teleH = 2140 - teleY;
        c.fillRoundRect(rightX, teleY, rightW, teleH, 14, rgba(252, 252, 248), rgba(40, 45, 55), 3);
        c.drawText(rightX + 20, teleY + 18, "LIVE SENSORS & OSCILLOSCOPE", 3, rgba(20, 24, 32), true, rightW - 40);

        // 2x3 Sensor Pill Grid (fits cleanly with zero overflow!)
        struct SensorPill {
            const char *label;
            char val[40];
            uint32_t col;
        } pills[6];

        snprintf(pills[0].val, sizeof(pills[0].val), "%.1f C / %.1f C", hw.cpuTempC, hw.battTempC);
        pills[0].label = "SOC / BATT TEMP"; pills[0].col = rgba(200, 45, 40);

        snprintf(pills[1].val, sizeof(pills[1].val), "%.1f%% (%.0fmV)", hw.battCapacityPct, hw.battVoltageMv);
        pills[1].label = "BATTERY LEVEL"; pills[1].col = rgba(25, 135, 75);

        snprintf(pills[2].val, sizeof(pills[2].val), "%+.0fmA (%+.0fmW)", hw.battCurrentMa, hw.battPowerMw);
        pills[2].label = "POWER DRAW"; pills[2].col = rgba(30, 95, 195);

        snprintf(pills[3].val, sizeof(pills[3].val), "%d%% | FL: %d", hw.markerBatteryPct, hw.frontlight);
        pills[3].label = "MARKER & LIGHT"; pills[3].col = rgba(155, 40, 145);

        snprintf(pills[4].val, sizeof(pills[4].val), "%d / %d MB", hw.memUsedMb, hw.memTotalMb);
        pills[4].label = "LPDDR4 RAM"; pills[4].col = rgba(40, 120, 165);

        snprintf(pills[5].val, sizeof(pills[5].val), "%.1f%% (%d MHz)", hw.totalCpuLoadPct, hw.cpuFreqMhz[0]);
        pills[5].label = "AVG CPU LOAD"; pills[5].col = rgba(210, 105, 20);

        int pillW = (rightW - 48) / 2;
        int pillH = 74;
        for (int p = 0; p < 6; p++) {
            int col = p % 2, row = p / 2;
            int px = rightX + 16 + col * (pillW + 16);
            int py = teleY + 60 + row * (pillH + 10);
            c.fillRoundRect(px, py, pillW, pillH, 10, rgba(240, 243, 248), pills[p].col, 2);
            c.drawText(px + 12, py + 10, pills[p].label, 2, rgba(75, 82, 95), true, pillW - 24);
            c.drawText(px + 12, py + 38, pills[p].val, 2, pills[p].col, true, pillW - 24);
        }

        // Tall Live Scrolling Oscilloscope Plot (y: teleY + 322 .. 2120)
        int gx = rightX + 16, gy = teleY + 322, gw = rightW - 32, gh = teleY + teleH - gy - 18;
        c.fillRect(gx, gy, gw, gh, rgba(18, 22, 30));
        c.drawRect(gx, gy, gw, gh, 2, rgba(80, 90, 110));
        for (int g = 1; g < 4; g++) {
            int ly = gy + (gh * g) / 4;
            c.fillRect(gx + 2, ly, gw - 4, 1, rgba(50, 60, 75));
        }
        c.drawText(gx + 14, gy + 12, "CYAN: CPU LOAD (0-100%)   RED: DIE TEMP (20-70C)", 2, rgba(220, 230, 245), true, gw - 28);

        int prevX = -1, prevYLoad = -1, prevYTemp = -1;
        for (int i = 0; i < HIST_LEN; i++) {
            int idx = (histHead + i) % HIST_LEN;
            int sx = gx + 8 + (i * (gw - 16)) / (HIST_LEN - 1);
            float loadNorm = std::clamp(histCpuLoad[idx] / 100.0f, 0.0f, 1.0f);
            float tempNorm = std::clamp((histCpuTemp[idx] - 20.0f) / 50.0f, 0.0f, 1.0f);
            int syLoad = gy + gh - 10 - (int)(loadNorm * (gh - 48));
            int syTemp = gy + gh - 10 - (int)(tempNorm * (gh - 48));
            if (prevX >= 0) {
                c.drawLine(prevX, prevYLoad, sx, syLoad, 3, rgba(55, 185, 255));
                c.drawLine(prevX, prevYTemp, sx, syTemp, 3, rgba(255, 80, 70));
            }
            prevX = sx;
            prevYLoad = syLoad;
            prevYTemp = syTemp;
        }
    }
};
