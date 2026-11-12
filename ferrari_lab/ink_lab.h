#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "qtfb_protocol.h"
#include "gfx2d.h"

class LowLatencyInkLab {
public:
    // Screen geometry constants (Portrait 1620x2160)
    static constexpr int TX = 24;
    static constexpr int TY = 180;
    static constexpr int TW = 1572;
    static constexpr int TH = 132;

    // Compact Stylus Stats Cluster inside Row 2 of Toolbar (x: 1196..1584, y: 250..304, 388x54)
    static constexpr int HX = 1196;
    static constexpr int HY = 250;
    static constexpr int HW = 388;
    static constexpr int HH = 54;

    // Paper Canvas Area (y: 320..2140, h = 1820)
    static constexpr int PX = 24;
    static constexpr int PY = 320;
    static constexpr int PW = 1572;
    static constexpr int PH = 1820; // PY + PH = 2140

    // Tool IDs
    enum ToolId {
        TOOL_FINELINER   = 0,
        TOOL_BALLPOINT   = 1,
        TOOL_CALLIGRAPHY = 2,
        TOOL_TILT_PENCIL = 3,
        TOOL_HIGHLIGHTER = 4,
        TOOL_ERASER      = 5
    };

    // Paper Template IDs
    enum TemplateId {
        TMPL_RULED     = 0,
        TMPL_DOT_GRID  = 1,
        TMPL_ENGR_GRID = 2
    };

    // Active state
    int activeTool       = TOOL_FINELINER;
    int activeColor      = 0; // 0=Black, 1=Cobalt, 2=Crimson, 3=Green, 4=Amber, 5=Violet
    int activeSize       = 1; // 0=S, 1=M, 2=L
    int activeTemplate   = TMPL_RULED;
    bool predictEnabled  = true;
    bool showStatsHud    = true;
    bool penDown         = false;
    bool activeStrokeIsEraser = false;
    int curRefreshMode   = REFRESH_MODE_CONTENT;

    // Live performance & sensor telemetry
    float samplesPerSec  = 240.0f;
    int dispatchUs       = 18;
    int lastStrokePts    = 0;
    float livePressure   = 0.0f; // 0..1
    int liveRawPressure  = 0;    // 0..4095
    int liveTiltX        = 0;    // -9000..9000 (hundredths of deg)
    int liveTiltY        = 0;
    float liveAzimuthDeg = 30.0f;
    float liveSlantDeg   = 0.0f;
    double lastHudUpdateSec = 0.0;
    double lastInkFlushSec  = 0.0;
    bool hudNeedsUpdate     = false;

    // UI Buttons
    UIButton btnTools[6];
    UIButton btnColors[6];
    UIButton btnSizes[3];
    UIButton btnTemplates[3];
    UIButton btnPredict;
    UIButton btnStats;
    UIButton btnUndo;
    UIButton btnClear;

    // Persistent paper + ink buffer (PW * PH)
    std::vector<uint32_t> paperBuf;

    // Copy-on-Write (CoW) 64x64 Tile Undo Ring Buffer (6 slots)
    static constexpr int TILE_SZ = 64;
    static constexpr int TILES_X = (PW + TILE_SZ - 1) / TILE_SZ; // 25
    static constexpr int TILES_Y = (PH + TILE_SZ - 1) / TILE_SZ; // 29
    static constexpr int TOTAL_TILES = TILES_X * TILES_Y;
    static constexpr int MAX_UNDO = 6;

    struct SavedTile {
        uint16_t tx, ty;
        uint32_t pixels[TILE_SZ * TILE_SZ];
    };

    struct UndoSnapshot {
        std::vector<SavedTile> tiles;
    };

    UndoSnapshot undoRing[MAX_UNDO];
    int undoCount = 0;
    int undoHead  = 0;
    UndoSnapshot currentStrokeUndo;
    bool tileSavedMask[TOTAL_TILES];

    // Stroke interpolation state (Midpoint Quadratic Bezier)
    struct StrokeSample {
        float x, y;
        float pressure;
        int tiltX, tiltY;
        float dirRad; // Stroke movement tangent angle
        double tSec;
    };

    StrokeSample p0, p1, p2;
    int strokeSampleCount = 0;
    double lastSampleTime = 0.0;
    float curStrokeSpeed  = 0.0f; // Smoothed point-to-point distance (px/sample) for xochitl EraserPipeline width scaling
    float smoothDx        = 1.0f;
    float smoothDy        = 0.0f;
    bool horizDominant    = true;
    uint32_t strokeId = 1;

    // Per-pixel stroke stamp guard for translucent highlighter so overlapping dabs
    // within a single stroke blend evenly instead of compounding
    std::vector<uint16_t> hiliteStampGrid;
    uint16_t curHiliteId = 1;

    // --- Non-Colliding 16x16 Screen-Aligned Swtcon Tile Pipeline (CONTENT / UI modes) ---
    // In libqsgepaper.so (EPFramebufferSwtcon::sendUpdate, 0x40e70), every update rect is aligned to
    // 4x2 (0x4fc10) and 16x16 (0x4fe40), updates swtcon's internal shadow buffer [0x90b80] immediately
    // (0x538f0), and runs active-pipe collision clipper 0x3cfe0 (0x410b0) which aborts/clips any active
    // hardware pipe in [swtcon+0x80] whose bounding rect overlaps the new update rect.
    // By tracking pipeBusyUntilSec on the exact 16x16 screen grid and dispatching at most one
    // 16x16-aligned non-busy rectangle per 24ms Qt frame, live monochrome ink (wf=4, ~145ms) is 100%
    // solid black with zero clipped pipes, and single-pass color accuracy conversion (wf=2, ~550ms)
    // smoothly trails ~180ms behind the moving pen stroke in real time!
    std::vector<uint8_t> colorSettleState;
    static constexpr int SWTCON_CELL = 16;
    static constexpr int SWTCON_GW   = (1620 + SWTCON_CELL - 1) / SWTCON_CELL; // 102
    static constexpr int SWTCON_GH   = (2160 + SWTCON_CELL - 1) / SWTCON_CELL; // 135

    struct SwtconCell {
        bool dirtyMono            = false;
        bool hasUnsettledColor    = false;
        bool hasColorPixels       = false;
        bool hasColorInFb         = false;
        uint32_t monoBatchId      = 0;
        uint32_t lastMonoStrokeId = 0;
        double firstDirtyMonoSec  = 0.0;
        double lastPenTouchSec    = 0.0;
        double pipeBusyUntilSec   = 0.0;
    };

    SwtconCell swtconGrid[SWTCON_GH][SWTCON_GW];
    uint32_t nextMonoBatchId       = 1;
    double lastSwtconDispatchSec   = 0.0;
    bool lastDispatchWasColor      = false;
    int consecutiveMonoDispatches  = 0;
    int consecutiveColorDispatches = 0;
    int lastDabBoundPx             = 6;

    struct ColorSettleSegment {
        int lx0, ly0, lx1, ly1;
        int sweepStage;
        double tReady;
        double tCreated;
    };
    std::vector<ColorSettleSegment> settleQueue;
    bool hasActiveSettleSeg = false;
    ColorSettleSegment activeSettleSeg = {0, 0, 0, 0, 0, 0.0, 0.0};

    // --- xochitl native eraser (FillMaskedEraser<uint32_t>, 0xd13a98..0xd13ae8 / init 0xd163e0) ---
    // While the eraser is down, xochitl paints a stride-2 masked overlay ONLY on pixels where
    // ((x + y) & 1) == 0: Black on even rows, White on odd rows. The other 50% of pixels are left
    // untouched. The overlay stays for the whole stroke; on pen-up the erased bounding box is
    // repainted from the clean document (paperBuf) in one region update.
    bool hasErasedPixels = false;
    int eraseBoxX0 = 0, eraseBoxY0 = 0, eraseBoxX1 = 0, eraseBoxY1 = 0; // local paper coords

    // Dirty screen bounding box for current incremental frame
    int dirtyX0 = 0, dirtyY0 = 0, dirtyX1 = 0, dirtyY1 = 0;

    // Transient 1-sample velocity-predicted tip region (written only to c.fb, restored from paperBuf)
    struct PredictedTipRect {
        bool active = false;
        int lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
    } predTip;

    LowLatencyInkLab()
        : paperBuf(PW * PH, rgba(255, 255, 255)),
          hiliteStampGrid(PW * PH, 0),
          colorSettleState(PW * PH, 0) {
        memset(tileSavedMask, 0, sizeof(tileSavedMask));
        layoutToolbarButtons();
        resetPaperToTemplate();
    }

    static inline double inkNowSec() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
    }

    void layoutToolbarButtons() {
        // Row 1 (y = 186, h = 56): 6 Tool Buttons across x = 36 .. 1584 (total width = 1548)
        const int r1Y = 186, r1H = 56;
        const int r1Gap = 12;
        const int r1W = (1548 - 5 * r1Gap) / 6; // 248 px each
        for (int i = 0; i < 6; i++) {
            btnTools[i] = {36 + i * (r1W + r1Gap), r1Y, r1W, r1H};
        }

        // Row 2 (y = 250, h = 54): Swatches, Sizes, Templates, Predict, Stats, Undo, Clear + Compact HUD on right
        const int r2Y = 250, r2H = 54;
        for (int i = 0; i < 6; i++) {
            btnColors[i] = {36 + i * (48 + 5), r2Y, 48, r2H}; // x: 36..349
        }
        for (int i = 0; i < 3; i++) {
            btnSizes[i] = {360 + i * (42 + 4), r2Y, 42, r2H}; // x: 360..494
        }
        for (int i = 0; i < 3; i++) {
            btnTemplates[i] = {504 + i * (94 + 5), r2Y, 94, r2H}; // x: 504..796
        }
        btnPredict = {804,  r2Y, 108, r2H};
        btnStats   = {918,  r2Y,  86, r2H};
        btnUndo    = {1010, r2Y,  84, r2H};
        btnClear   = {1100, r2Y,  86, r2H};
        // Compact Stylus Stats Pill occupies x = 1196 .. 1584 (width = 388, height = 54)
    }

    // Compute human-readable tilt slant (0..90 deg) and screen-aligned nib azimuth (-180..180 deg)
    void updateTiltMetrics(int tx, int ty, float fallbackDirRad = -0.68f) {
        liveTiltX = tx;
        liveTiltY = ty;
        float mag = hypotf((float)tx, (float)ty);
        // Elan digitizer reports tilt in hundredths of a degree (-9000..9000)
        if (mag > 180.0f) {
            liveSlantDeg = std::clamp(mag / 100.0f, 0.0f, 90.0f);
        } else {
            liveSlantDeg = std::clamp(mag, 0.0f, 90.0f);
        }
        if (mag > 150.0f) {
            // Negate ty so screen-space (+Y down) rotation matches physical Marker rotation direction
            liveAzimuthDeg = atan2f(-(float)ty, (float)tx) * (180.0f / 3.14159265f);
        } else {
            liveAzimuthDeg = fallbackDirRad * (180.0f / 3.14159265f);
        }
    }

    // Returns the clean paper template pixel at local coordinates (lx, ly).
    // Strictly returns pure Black (0xFF000000) or pure White (0xFFFFFFFF) so libqsgepaper.so's
    // hardware per-pixel mask generator (0x4fe40, pixelMode=16) masks out 100% of template & paper
    // pixels (m_mask = 0x00) during CONTENT/UI color ink settling!
    inline uint32_t templatePixelAt(int lx, int ly) const {
        const uint32_t bg = rgba(255, 255, 255);
        const uint32_t fg = rgba(0, 0, 0);
        if (activeTemplate == TMPL_RULED) {
            if (lx == 148 || lx == 152) {
                return ((ly & 1) == 0) ? fg : bg;
            }
            if (ly == 56 || ly == 59) {
                return (lx >= 36 && lx < PW - 36 && ((lx & 1) == 0)) ? fg : bg;
            }
            if (ly > 59 && ly < PH - 36 && ((ly - 59) % 46 == 0)) {
                return (lx >= 36 && lx < PW - 36 && ((lx & 1) == 0)) ? fg : bg;
            }
            return bg;
        } else if (activeTemplate == TMPL_DOT_GRID) {
            if (lx >= 36 && lx < PW - 36 && ly >= 36 && ly < PH - 36) {
                int gx = (lx - 36) % 40;
                int gy = (ly - 36) % 40;
                if (gx < 2 && gy < 2) {
                    return fg;
                }
            }
            return bg;
        } else {
            if (lx >= 36 && lx <= PW - 36 && ly >= 36 && ly <= PH - 36) {
                int gx = lx - 36;
                int gy = ly - 36;
                if (gx == 0 || gy == 0 || lx == PW - 36 || ly == PH - 36 ||
                    (gx % 100 == 0) || (gy % 100 == 0)) {
                    return (((lx + ly) & 1) == 0) ? fg : bg;
                }
                if ((gx % 20 == 0) || (gy % 20 == 0)) {
                    return (((lx + ly) & 3) == 0) ? fg : bg;
                }
            }
            return bg;
        }
    }

    void clearSwtconGrid() {
        for (int cy = 0; cy < SWTCON_GH; cy++) {
            for (int cx = 0; cx < SWTCON_GW; cx++) {
                swtconGrid[cy][cx] = SwtconCell{};
            }
        }
        consecutiveMonoDispatches = 0;
        consecutiveColorDispatches = 0;
        lastDispatchWasColor = false;
    }

    bool hasPendingSwtconWork() const {
        const int cy0 = PY >> 4;
        const int cy1 = std::min(SWTCON_GH, (PY + PH + 15) >> 4);
        const int cx0 = PX >> 4;
        const int cx1 = std::min(SWTCON_GW, (PX + PW + 15) >> 4);
        for (int cy = cy0; cy < cy1; cy++) {
            for (int cx = cx0; cx < cx1; cx++) {
                if (swtconGrid[cy][cx].dirtyMono || swtconGrid[cy][cx].hasUnsettledColor) {
                    return true;
                }
            }
        }
        return false;
    }

    void resetPaperToTemplate() {
        for (int ly = 0; ly < PH; ly++) {
            uint32_t *row = paperBuf.data() + ly * PW;
            for (int lx = 0; lx < PW; lx++) {
                row[lx] = templatePixelAt(lx, ly);
            }
        }
        std::fill(colorSettleState.begin(), colorSettleState.end(), 0);
        settleQueue.clear();
        hasActiveSettleSeg = false;
        clearSwtconGrid();
    }

    inline void flushActiveSettleSegment(double delaySec) {
        (void)delaySec;
        hasActiveSettleSeg = false;
    }

    inline void recordColorSettlePixel(int lx, int ly) {
        if ((unsigned)lx >= (unsigned)PW || (unsigned)ly >= (unsigned)PH) return;
        colorSettleState[ly * PW + lx] = 1;
        int sx = PX + lx;
        int sy = PY + ly;
        SwtconCell &cell = swtconGrid[sy >> 4][sx >> 4];
        cell.hasUnsettledColor = true;
        cell.hasColorPixels = true;
    }

    static inline void getInkRgb(int colorIdx, uint8_t &r, uint8_t &g, uint8_t &b) {
        switch (colorIdx) {
            case 0: r = 0;   g = 0;   b = 0;   break; // Pure Black (0xFF000000 for hardware B/W mask & wf=4)
            case 1: r = 20;  g = 72;  b = 218; break; // Cobalt Blue
            case 2: r = 218; g = 28;  b = 38;  break; // Crimson Red
            case 3: r = 20;  g = 148; b = 62;  break; // Forest Green
            case 4: r = 248; g = 212; b = 16;  break; // Solar Yellow
            case 5: r = 152; g = 36;  b = 188; break; // Violet
            default: r = 0;  g = 0;   b = 0;   break;
        }
    }

    static inline void getHighlighterRgb(int colorIdx, uint8_t &r, uint8_t &g, uint8_t &b) {
        switch (colorIdx) {
            case 0: r = 255; g = 238; b = 55;  break; // Fluorescent Yellow (when Black swatch selected)
            case 1: r = 115; g = 195; b = 255; break; // Sky Blue Highlighter
            case 2: r = 255; g = 140; b = 155; break; // Coral Pink Highlighter
            case 3: r = 125; g = 238; b = 145; break; // Mint Green Highlighter
            case 4: r = 255; g = 205; b = 70;  break; // Warm Amber Highlighter
            case 5: r = 210; g = 150; b = 255; break; // Lavender Highlighter
            default: r = 255; g = 238; b = 55; break;
        }
    }

    inline float getSizeScale() const {
        if (activeSize == 0) return 0.42f; // Ultra-fine S tip
        if (activeSize == 2) return 1.55f; // Broad L tip
        return 0.92f;                      // Medium M tip
    }

    // --- Copy-on-Write Tile Undo Management ---
    void beginUndoCapture() {
        currentStrokeUndo.tiles.clear();
        memset(tileSavedMask, 0, sizeof(tileSavedMask));
    }

    inline void ensureTileSaved(int lx, int ly) {
        int tx = lx >> 6;
        int ty = ly >> 6;
        int idx = ty * TILES_X + tx;
        if (tileSavedMask[idx]) return;
        tileSavedMask[idx] = true;

        SavedTile st;
        st.tx = (uint16_t)tx;
        st.ty = (uint16_t)ty;
        int x0 = tx * TILE_SZ;
        int y0 = ty * TILE_SZ;
        int copyW = std::min(TILE_SZ, PW - x0);
        int copyH = std::min(TILE_SZ, PH - y0);
        for (int r = 0; r < copyH; r++) {
            memcpy(&st.pixels[r * TILE_SZ], &paperBuf[(y0 + r) * PW + x0], copyW * sizeof(uint32_t));
        }
        currentStrokeUndo.tiles.push_back(st);
    }

    void commitUndoCapture() {
        if (currentStrokeUndo.tiles.empty()) return;
        undoRing[undoHead] = std::move(currentStrokeUndo);
        currentStrokeUndo.tiles.clear();
        undoHead = (undoHead + 1) % MAX_UNDO;
        if (undoCount < MAX_UNDO) undoCount++;
    }

    bool performUndo(Canvas2D &c, qtfb::Connection &conn, const RawMarkerState &marker) {
        if (undoCount <= 0) return false;
        undoHead = (undoHead - 1 + MAX_UNDO) % MAX_UNDO;
        undoCount--;

        std::fill(colorSettleState.begin(), colorSettleState.end(), 0);
        settleQueue.clear();
        hasActiveSettleSeg = false;
        for (int cy = 0; cy < SWTCON_GH; cy++) {
            for (int cx = 0; cx < SWTCON_GW; cx++) {
                swtconGrid[cy][cx].dirtyMono = false;
                swtconGrid[cy][cx].hasUnsettledColor = false;
                swtconGrid[cy][cx].hasColorInFb = swtconGrid[cy][cx].hasColorPixels;
            }
        }

        UndoSnapshot &snap = undoRing[undoHead];
        int ux0 = PW, uy0 = PH, ux1 = 0, uy1 = 0;
        for (const SavedTile &st : snap.tiles) {
            int x0 = st.tx * TILE_SZ;
            int y0 = st.ty * TILE_SZ;
            int copyW = std::min(TILE_SZ, PW - x0);
            int copyH = std::min(TILE_SZ, PH - y0);
            for (int r = 0; r < copyH; r++) {
                int ly = y0 + r;
                const uint32_t *src = &st.pixels[r * TILE_SZ];
                memcpy(&paperBuf[ly * PW + x0], src, copyW * sizeof(uint32_t));
                memcpy(&c.fb[(PY + ly) * c.W + (PX + x0)], src, copyW * sizeof(uint32_t));
            }
            ux0 = std::min(ux0, x0);
            uy0 = std::min(uy0, y0);
            ux1 = std::max(ux1, x0 + copyW);
            uy1 = std::max(uy1, y0 + copyH);
        }
        snap.tiles.clear();

        drawToolbar(c, marker);
        conn.updateRegion(TX, TY, TW, TH);
        if (ux1 > ux0 && uy1 > uy0) {
            conn.updateRegion(PX + ux0, PY + uy0, ux1 - ux0, uy1 - uy0);
        }
        lastSwtconDispatchSec = inkNowSec();
        return true;
    }

    // --- Dirty Region & Pixel Dual-Write Helpers ---
    inline void resetDirtyRect() {
        dirtyX0 = 100000;
        dirtyY0 = 100000;
        dirtyX1 = -100000;
        dirtyY1 = -100000;
    }

    inline void markDirtyScreen(int sx, int sy) {
        if (sx < dirtyX0) dirtyX0 = sx;
        if (sx + 1 > dirtyX1) dirtyX1 = sx + 1;
        if (sy < dirtyY0) dirtyY0 = sy;
        if (sy + 1 > dirtyY1) dirtyY1 = sy + 1;
    }

    inline void markDirtyLocalRect(int lx0, int ly0, int lx1, int ly1) {
        int sx0 = PX + std::max(0, lx0);
        int sy0 = PY + std::max(0, ly0);
        int sx1 = PX + std::min(PW, lx1);
        int sy1 = PY + std::min(PH, ly1);
        if (sx0 < dirtyX0) dirtyX0 = sx0;
        if (sx1 > dirtyX1) dirtyX1 = sx1;
        if (sy0 < dirtyY0) dirtyY0 = sy0;
        if (sy1 > dirtyY1) dirtyY1 = sy1;
    }

    inline void writeInkPixel(Canvas2D &c, int lx, int ly, uint32_t col, bool transientOnly) {
        if ((unsigned)lx >= (unsigned)PW || (unsigned)ly >= (unsigned)PH) return;
        int sx = PX + lx;
        int sy = PY + ly;
        bool isContentOrUi = (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI);
        // Never write transient speculative pixels in stateful CONTENT/UI mode, as restoring them
        // 4ms later oscillates WHITE->BLACK->WHITE in swtcon's [0x90b80] shadow buffer.
        if (transientOnly && isContentOrUi) return;

        bool isColorStroke = !activeStrokeIsEraser && (activeColor != 0 || activeTool == TOOL_HIGHLIGHTER);
        bool useColorSettle = isColorStroke && isContentOrUi;
        if (!transientOnly) {
            ensureTileSaved(lx, ly);
            paperBuf[ly * PW + lx] = col;
            int cx = sx >> 4;
            int cy = sy >> 4;
            double now = (lastSampleTime > 0.0) ? lastSampleTime : inkNowSec();
            if (col != rgba(0, 0, 0) && col != rgba(255, 255, 255)) {
                swtconGrid[cy][cx].hasColorPixels = true;
            }
            if (useColorSettle) {
                // Stage in colorSettleState (2 = black lead pixel, 3 = highlighter white checker pixel)
                // WITHOUT writing to c.fb yet! Writing to c.fb before doMono dispatches the cell allows
                // QtQuick's FBController::paint(QPainter*) to copy premature black pixels into swtcon's
                // [0x90b80] shadow buffer if an adjacent update rect overlaps or unions near the pen tip.
                uint8_t targetSt = (activeTool == TOOL_HIGHLIGHTER && (((lx + ly) & 1) != 0)) ? 3 : 2;
                uint8_t &st = colorSettleState[ly * PW + lx];
                SwtconCell &cell = swtconGrid[cy][cx];
                if (st != 1 || (now - cell.lastPenTouchSec) > 0.080) {
                    st = targetSt;
                    if (!cell.dirtyMono) {
                        cell.dirtyMono = true;
                        cell.firstDirtyMonoSec = now;
                        cell.monoBatchId = 0;
                    }
                    cell.lastPenTouchSec = now;
                }
                cell.hasUnsettledColor = true;
            } else if (isContentOrUi) {
                // Stage pure B/W CONTENT/UI pixel (state 4) so c.fb is updated atomically inside doMono.
                colorSettleState[ly * PW + lx] = 4;
                SwtconCell &cell = swtconGrid[cy][cx];
                if (!cell.dirtyMono) {
                    cell.dirtyMono = true;
                    cell.firstDirtyMonoSec = now;
                }
                cell.lastPenTouchSec = now;
            } else {
                colorSettleState[ly * PW + lx] = 0;
                c.fb[sy * c.W + sx] = col;
            }
        } else {
            if (!predTip.active) {
                predTip.active = true;
                predTip.lx0 = lx;
                predTip.ly0 = ly;
                predTip.lx1 = lx + 1;
                predTip.ly1 = ly + 1;
            } else {
                if (lx < predTip.lx0) predTip.lx0 = lx;
                if (ly < predTip.ly0) predTip.ly0 = ly;
                if (lx + 1 > predTip.lx1) predTip.lx1 = lx + 1;
                if (ly + 1 > predTip.ly1) predTip.ly1 = ly + 1;
            }
            c.fb[sy * c.W + sx] = col;
        }
        markDirtyScreen(sx, sy);
    }

    // xochitl FillMaskedEraser<uint32_t> (0xd13a98..0xd13ae8): erase the document pixel, and paint the
    // masked overlay on screen ONLY where ((x + y) & 1) == 0 (Black on even rows, White on odd rows).
    inline void writeEraserTrailPixel(Canvas2D &c, int lx, int ly) {
        if ((unsigned)lx >= (unsigned)PW || (unsigned)ly >= (unsigned)PH) return;
        int sx = PX + lx;
        int sy = PY + ly;

        ensureTileSaved(lx, ly);
        paperBuf[ly * PW + lx] = templatePixelAt(lx, ly);
        colorSettleState[ly * PW + lx] = 0;

        if (!hasErasedPixels) {
            hasErasedPixels = true;
            eraseBoxX0 = lx; eraseBoxY0 = ly; eraseBoxX1 = lx + 1; eraseBoxY1 = ly + 1;
        } else {
            if (lx < eraseBoxX0) eraseBoxX0 = lx;
            if (ly < eraseBoxY0) eraseBoxY0 = ly;
            if (lx + 1 > eraseBoxX1) eraseBoxX1 = lx + 1;
            if (ly + 1 > eraseBoxY1) eraseBoxY1 = ly + 1;
        }

        if (((sx + sy) & 1) == 0) {
            c.fb[sy * c.W + sx] = (sy & 1) ? rgba(255, 255, 255) : rgba(0, 0, 0);
            if (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI) {
                SwtconCell &cell = swtconGrid[sy >> 4][sx >> 4];
                double now = (lastSampleTime > 0.0) ? lastSampleTime : inkNowSec();
                if (!cell.dirtyMono) {
                    cell.dirtyMono = true;
                    cell.firstDirtyMonoSec = now;
                }
                cell.lastPenTouchSec = now;
            }
            markDirtyScreen(sx, sy);
        }
    }

    // Pen-up settle: replace the FillMaskedEraser overlay with the clean document across the erased box
    void settleEraserStroke(Canvas2D &c) {
        if (!hasErasedPixels) return;
        int x0 = std::clamp(eraseBoxX0, 0, PW), x1 = std::clamp(eraseBoxX1, 0, PW);
        int y0 = std::clamp(eraseBoxY0, 0, PH), y1 = std::clamp(eraseBoxY1, 0, PH);
        bool isContentOrUi = (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI);
        double now = inkNowSec();
        const uint32_t pureBlack = rgba(0, 0, 0);
        const uint32_t pureWhite = rgba(255, 255, 255);
        for (int ly = y0; ly < y1; ly++) {
            int sy = PY + ly;
            uint32_t *fbRow = &c.fb[sy * c.W + PX];
            const uint32_t *pRow = &paperBuf[ly * PW];
            uint8_t *stRow = &colorSettleState[ly * PW];
            for (int lx = x0; lx < x1; lx++) {
                uint32_t col = pRow[lx];
                int sx = PX + lx;
                SwtconCell &cell = swtconGrid[sy >> 4][sx >> 4];
                if (isContentOrUi && col != pureBlack && col != pureWhite) {
                    stRow[lx] = 2;
                    cell.hasUnsettledColor = true;
                    cell.hasColorPixels = true;
                } else if (isContentOrUi) {
                    stRow[lx] = 4;
                } else {
                    fbRow[lx] = col;
                    stRow[lx] = 0;
                }
                if (isContentOrUi) {
                    if (!cell.dirtyMono) {
                        cell.dirtyMono = true;
                        cell.firstDirtyMonoSec = now;
                    }
                    cell.lastPenTouchSec = now;
                }
            }
        }
        markDirtyLocalRect(x0, y0, x1, y1);
        hasErasedPixels = false;
    }

    void restorePredictedTip(Canvas2D &c) {
        if (!predTip.active) return;
        int x0 = std::clamp(predTip.lx0, 0, PW);
        int y0 = std::clamp(predTip.ly0, 0, PH);
        int x1 = std::clamp(predTip.lx1, 0, PW);
        int y1 = std::clamp(predTip.ly1, 0, PH);
        for (int ly = y0; ly < y1; ly++) {
            uint32_t *dst = &c.fb[(PY + ly) * c.W + (PX + x0)];
            const uint32_t *src = &paperBuf[ly * PW + x0];
            const uint8_t *st = &colorSettleState[ly * PW + x0];
            for (int i = 0; i < (x1 - x0); i++) {
                if (st[i] != 0) {
                    int lx = x0 + i;
                    dst[i] = (activeTool == TOOL_HIGHLIGHTER && (((lx + ly) & 1) != 0))
                             ? rgba(255, 255, 255)
                             : rgba(0, 0, 0);
                } else {
                    dst[i] = src[i];
                }
            }
        }
        markDirtyLocalRect(x0, y0, x1, y1);
        predTip.active = false;
    }

    inline uint32_t resolveInkColor(int lx, int ly, uint8_t r, uint8_t g, uint8_t b) const {
        if (activeColor == 0 && activeTool != TOOL_HIGHLIGHTER) {
            return rgba(0, 0, 0);
        }
        if (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI) {
            // Pass un-dithered RGB so libqsgepaper.so's hardware mask generator (0x4fe40) sees 100%
            // of stroke pixels as Class 4 Color (m_mask = 0xFF) and dithers via the factory 33x33x33
            // tetrahedral Gallery 3 LUT (0x4e8a0)!
            return rgba(r, g, b);
        }
        return ditherGallery3(PX + lx, PY + ly, r, g, b);
    }

    // Compute chisel nib orientation angle & tilt slant factor from Elan Marker Plus tiltX/tiltY
    static inline void computeNibOrientation(int tiltX, int tiltY, float fallbackDirRad,
                                             float &outNibAngle, float &outTiltNorm) {
        float rawMag = hypotf((float)tiltX, (float)tiltY);
        // Elan Marker Plus reports tilt in hundredths of a degree (-9000..9000)
        if (rawMag > 180.0f) {
            outTiltNorm = std::clamp(rawMag / 6000.0f, 0.0f, 1.0f);
        } else {
            outTiltNorm = std::clamp(rawMag / 60.0f, 0.0f, 1.0f);
        }
        if (rawMag > 150.0f) {
            // Negate tiltY so screen-space (+Y down) rotation matches physical Marker rotation direction
            outNibAngle = atan2f(-(float)tiltY, (float)tiltX) + 1.5707963f;
        } else {
            outNibAngle = fallbackDirRad;
        }
    }

    // Stamp a single brush dab at local paper coordinates (fx, fy)
    void stampDab(Canvas2D &c, float fx, float fy, float pressure, int tiltX, int tiltY,
                  float strokeDirRad, bool isEraser, bool transientOnly) {
        int cx = (int)(fx + 0.5f);
        int cy = (int)(fy + 0.5f);
        if (cx < -48 || cx >= PW + 48 || cy < -48 || cy >= PH + 48) return;

        float sz = getSizeScale();
        int effTool = isEraser ? TOOL_ERASER : activeTool;

        if (effTool == TOOL_ERASER) {
            // xochitl strokev2::EraserPipeline (0xd1a280..0xd1a35c) velocity + tilt cubic Hermite width scaling:
            // Clamps point-to-point speed v to [2.0, 10.0], normalizes t = (v - 2.0) * 0.125,
            // and applies smoothstep H(t) = t^2 * (3 - 2*t) so slow moves are surgical and fast scrubs widen!
            float vClamped = std::clamp(curStrokeSpeed, 2.0f, 10.0f);
            float tSpeed   = (vClamped - 2.0f) * 0.125f;
            float hermite  = tSpeed * tSpeed * (3.0f - 2.0f * tSpeed);

            float nibAngle = 0.0f, tiltNorm = 0.0f;
            computeNibOrientation(tiltX, tiltY, strokeDirRad, nibAngle, tiltNorm);

            float sizeBase = (activeSize == 0) ? 10.0f : ((activeSize == 1) ? 16.0f : 24.0f);
            float dynRad   = sizeBase * (0.72f + 1.15f * hermite + 0.28f * tiltNorm + 0.18f * pressure);
            int rad = std::clamp((int)(dynRad + 0.5f), 7, 52);
            lastDabBoundPx = rad + 2;
            int r2 = rad * rad;
            int y0 = std::max(0, cy - rad), y1 = std::min(PH - 1, cy + rad);
            int x0 = std::max(0, cx - rad), x1 = std::min(PW - 1, cx + rad);
            for (int ly = y0; ly <= y1; ly++) {
                int dy = ly - cy;
                int dy2 = dy * dy;
                for (int lx = x0; lx <= x1; lx++) {
                    int dx = lx - cx;
                    int d2 = dx * dx + dy2;
                    if (d2 <= r2) {
                        writeEraserTrailPixel(c, lx, ly);
                    }
                }
            }
            return;
        }

        uint8_t ir, ig, ib;
        getInkRgb(activeColor, ir, ig, ib);

        if (effTool == TOOL_FINELINER) {
            float baseR = (activeSize == 0) ? 0.58f : ((activeSize == 1) ? 1.38f : 2.45f);
            float radF = baseR * (pressure < 0.12f ? (0.65f + pressure * 2.9f) : 1.0f);
            int rad = std::max(1, (int)ceilf(radF));
            lastDabBoundPx = rad + 2;
            float r2Max = (radF + 0.25f) * (radF + 0.25f);
            int y0 = std::max(0, cy - rad), y1 = std::min(PH - 1, cy + rad);
            int x0 = std::max(0, cx - rad), x1 = std::min(PW - 1, cx + rad);
            for (int ly = y0; ly <= y1; ly++) {
                float dy = (float)ly - fy;
                for (int lx = x0; lx <= x1; lx++) {
                    float dx = (float)lx - fx;
                    if ((lx == cx && ly == cy) || (dx * dx + dy * dy <= r2Max)) {
                        writeInkPixel(c, lx, ly, resolveInkColor(lx, ly, ir, ig, ib), transientOnly);
                    }
                }
            }
        } else if (effTool == TOOL_BALLPOINT) {
            float radF;
            if (activeSize == 0) {
                radF = 0.42f + pressure * 0.72f; // Fine 0.38mm ballpoint
            } else if (activeSize == 1) {
                radF = 0.80f + pressure * 1.65f;
            } else {
                radF = 1.25f + pressure * 2.75f;
            }
            int rad = std::max(1, (int)ceilf(radF));
            lastDabBoundPx = rad + 2;
            float r2Max = (radF + 0.18f) * (radF + 0.18f);
            int densityThresh = (int)(std::clamp(0.42f + pressure * 0.78f, 0.42f, 1.0f) * 64.0f);
            int y0 = std::max(0, cy - rad), y1 = std::min(PH - 1, cy + rad);
            int x0 = std::max(0, cx - rad), x1 = std::min(PW - 1, cx + rad);
            for (int ly = y0; ly <= y1; ly++) {
                float dy = (float)ly - fy;
                for (int lx = x0; lx <= x1; lx++) {
                    float dx = (float)lx - fx;
                    float d2 = dx * dx + dy * dy;
                    if ((lx == cx && ly == cy) || d2 <= r2Max) {
                        if ((lx == cx && ly == cy && activeSize == 0) || densityThresh >= 62 || BAYER8[ly & 7][lx & 7] < densityThresh) {
                            writeInkPixel(c, lx, ly, resolveInkColor(lx, ly, ir, ig, ib), transientOnly);
                        }
                    }
                }
            }
        } else if (effTool == TOOL_CALLIGRAPHY) {
            // Angle-responsive Chisel Nib:
            // Nib angle rotates in lockstep with Marker Plus 3D tilt azimuth!
            float nibAngle = -0.68f, tiltNorm = 0.0f;
            computeNibOrientation(tiltX, tiltY, -0.68f, nibAngle, tiltNorm);

            float angleDiff = fabsf(sinf(strokeDirRad - nibAngle));
            float halfSpan  = (4.8f + pressure * 9.5f + tiltNorm * 6.5f) * sz;
            float halfThick = (0.85f + pressure * 0.80f + angleDiff * 0.60f) * sz;
            float cosA = cosf(nibAngle), sinA = sinf(nibAngle);
            int bound = (int)ceilf(halfSpan + halfThick + 1.0f);
            lastDabBoundPx = bound + 2;
            int y0 = std::max(0, cy - bound), y1 = std::min(PH - 1, cy + bound);
            int x0 = std::max(0, cx - bound), x1 = std::min(PW - 1, cx + bound);
            for (int ly = y0; ly <= y1; ly++) {
                float dy = (float)ly - fy;
                for (int lx = x0; lx <= x1; lx++) {
                    float dx = (float)lx - fx;
                    float u = fabsf(dx * cosA + dy * sinA);
                    float v = fabsf(-dx * sinA + dy * cosA);
                    if ((lx == cx && ly == cy) || (u <= halfSpan && v <= halfThick)) {
                        writeInkPixel(c, lx, ly, resolveInkColor(lx, ly, ir, ig, ib), transientOnly);
                    }
                }
            }
        } else if (effTool == TOOL_TILT_PENCIL) {
            float nibAngle = 0.55f, tiltNorm = 0.0f;
            computeNibOrientation(tiltX, tiltY, 0.55f, nibAngle, tiltNorm);
            float tiltAng = nibAngle - 1.5707963f;
            float cosA = cosf(tiltAng), sinA = sinf(tiltAng);

            // Only engage broad side-shading when Marker is tilted flatter than normal writing angle (>32 deg slant)
            float shadeTilt = std::max(0.0f, (tiltNorm - 0.53f) / 0.47f);

            float rMajor, rMinor, baseDensity;
            if (activeSize == 0) {
                // Ultra-fine S mechanical pencil: razor-sharp 1..1.7px tip at normal writing angles!
                rMinor = 0.38f + pressure * 0.42f + shadeTilt * 0.85f;
                rMajor = 0.45f + pressure * 0.50f + shadeTilt * 3.10f;
                baseDensity = std::clamp(0.48f + pressure * 0.52f - shadeTilt * 0.22f, 0.32f, 0.98f);
            } else if (activeSize == 1) {
                // Medium M graphite pencil
                rMinor = 0.95f + pressure * 1.05f + shadeTilt * 2.10f;
                rMajor = 1.25f + pressure * 1.35f + shadeTilt * 7.20f;
                baseDensity = std::clamp((0.26f + pressure * 0.72f) * (1.0f - shadeTilt * 0.25f), 0.16f, 0.95f);
            } else {
                // Broad L sketching/shading pencil
                rMinor = 1.75f + pressure * 1.85f + shadeTilt * 3.80f;
                rMajor = 2.30f + pressure * 2.40f + shadeTilt * 13.50f;
                baseDensity = std::clamp((0.22f + pressure * 0.75f) * (1.0f - shadeTilt * 0.28f), 0.14f, 0.95f);
            }

            int bound = std::max(1, (int)ceilf(rMajor + 0.75f));
            lastDabBoundPx = bound + 2;
            int y0 = std::max(0, cy - bound), y1 = std::min(PH - 1, cy + bound);
            int x0 = std::max(0, cx - bound), x1 = std::min(PW - 1, cx + bound);
            for (int ly = y0; ly <= y1; ly++) {
                float dy = (float)ly - fy;
                for (int lx = x0; lx <= x1; lx++) {
                    bool isCorePixel = (lx == cx && ly == cy && shadeTilt < 0.25f);
                    float dx = (float)lx - fx;
                    float u = (dx * cosA + dy * sinA) / rMajor;
                    float v = (-dx * sinA + dy * cosA) / rMinor;
                    float d2 = u * u + v * v;
                    if (isCorePixel || d2 <= 1.0f) {
                        float falloff = isCorePixel ? 1.0f : (1.0f - d2 * 0.65f);
                        int thresh = (int)(baseDensity * falloff * 64.0f);
                        uint32_t h = ((uint32_t)lx * 73856093u) ^ ((uint32_t)ly * 19349663u);
                        int grain = ((int)BAYER8[ly & 7][lx & 7] + (int)(h & 31u)) >> 1;
                        if ((activeSize == 0 && isCorePixel && pressure > 0.12f) || grain < thresh) {
                            writeInkPixel(c, lx, ly, resolveInkColor(lx, ly, ir, ig, ib), transientOnly);
                        }
                    }
                }
            }
        } else if (effTool == TOOL_HIGHLIGHTER) {
            uint8_t hr, hg, hb;
            getHighlighterRgb(activeColor, hr, hg, hb);

            float nibAngle = 1.35f, tiltNorm = 0.0f;
            computeNibOrientation(tiltX, tiltY, strokeDirRad + 1.5707963f, nibAngle, tiltNorm);

            float halfSpan  = (8.5f + tiltNorm * 13.5f + pressure * 4.5f) * sz;
            float halfThick = (2.2f + tiltNorm * 1.8f) * sz;
            float cosA = cosf(nibAngle), sinA = sinf(nibAngle);
            int bound = (int)ceilf(halfSpan + halfThick + 1.0f);
            lastDabBoundPx = bound + 2;

            int y0 = std::max(0, cy - bound), y1 = std::min(PH - 1, cy + bound);
            int x0 = std::max(0, cx - bound), x1 = std::min(PW - 1, cx + bound);

            for (int ly = y0; ly <= y1; ly++) {
                float dy = (float)ly - fy;
                for (int lx = x0; lx <= x1; lx++) {
                    float dx = (float)lx - fx;
                    float u = fabsf(dx * cosA + dy * sinA);
                    float v = fabsf(-dx * sinA + dy * cosA);
                    if (u <= halfSpan && v <= halfThick) {
                        int gIdx = ly * PW + lx;
                        if (!transientOnly) {
                            if (hiliteStampGrid[gIdx] == curHiliteId) continue;
                            hiliteStampGrid[gIdx] = curHiliteId;
                        }
                        uint32_t dst = paperBuf[gIdx];
                        int dr = dst & 0xFF;
                        int dg = (dst >> 8) & 0xFF;
                        int db = (dst >> 16) & 0xFF;
                        int lum = (dr * 77 + dg * 150 + db * 29) >> 8;
                        if (lum < 110) continue;

                        uint32_t hiCol = (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI)
                                         ? rgba(hr, hg, hb)
                                         : ditherGallery3(PX + lx, PY + ly, hr, hg, hb);
                        writeInkPixel(c, lx, ly, hiCol, transientOnly);
                    }
                }
            }
        }
    }

    // Tessellate a quadratic Bezier curve B(t) = (1-t)^2 * A + 2(1-t)t * Ctrl + t^2 * B
    void tessellateQuadraticBezier(Canvas2D &c,
                                   const StrokeSample &A,
                                   const StrokeSample &Ctrl,
                                   const StrokeSample &B,
                                   bool isEraser,
                                   bool transientOnly) {
        float d1 = hypotf(Ctrl.x - A.x, Ctrl.y - A.y);
        float d2 = hypotf(B.x - Ctrl.x, B.y - Ctrl.y);
        float approxLen = d1 + d2;
        int steps = std::max(1, (int)ceilf(approxLen * 1.25f));

        float chordDx = B.x - A.x;
        float chordDy = B.y - A.y;
        float dirRad = (hypotf(chordDx, chordDy) > 0.5f) ? atan2f(chordDy, chordDx) : Ctrl.dirRad;

        for (int s = 0; s <= steps; s++) {
            float t = (float)s / (float)steps;
            float u = 1.0f - t;
            float w0 = u * u;
            float w1 = 2.0f * u * t;
            float w2 = t * t;

            float fx = w0 * A.x + w1 * Ctrl.x + w2 * B.x;
            float fy = w0 * A.y + w1 * Ctrl.y + w2 * B.y;
            float fp = w0 * A.pressure + w1 * Ctrl.pressure + w2 * B.pressure;
            int ftx  = (int)(w0 * A.tiltX + w1 * Ctrl.tiltX + w2 * B.tiltX);
            int fty  = (int)(w0 * A.tiltY + w1 * Ctrl.tiltY + w2 * B.tiltY);

            stampDab(c, fx, fy, fp, ftx, fty, dirRad, isEraser, transientOnly);
        }
    }

    static inline StrokeSample midpointSample(const StrokeSample &a, const StrokeSample &b) {
        return {
            0.5f * (a.x + b.x),
            0.5f * (a.y + b.y),
            0.5f * (a.pressure + b.pressure),
            (a.tiltX + b.tiltX) / 2,
            (a.tiltY + b.tiltY) / 2,
            b.dirRad,
            0.5 * (a.tSec + b.tSec)
        };
    }

    // Unified Non-Colliding 16x16 Screen-Aligned Swtcon Scheduler for CONTENT and UI modes.
    // Guarantees:
    //  1. >= 24ms between conn.updateRegion calls so QtQuick (60Hz) never unions separate rects.
    //  2. Zero overlap with active swtcon hardware pipes (now >= pipeBusyUntilSec), defeating 0x3cfe0
    //     pipe clipping so live monochrome ink (wf=4, ~145ms) is 100% solid black with zero white cutouts.
    //  3. Interleaved single-pass color accuracy conversion (wf=2, ~550ms) trailing ~180ms behind
    //     the moving pen tip in real time while the stroke is being drawn!
    void stepSwtconPipeline(Canvas2D &c, qtfb::Connection &conn) {
        double now = inkNowSec();
        double minDispatchInterval = lastDispatchWasColor ? 0.030 : 0.024;
        if ((now - lastSwtconDispatchSec) < minDispatchInterval) {
            return;
        }

        const int minCx = PX >> 4;
        const int maxCx = std::min(SWTCON_GW, (PX + PW + 15) >> 4);
        const int minCy = PY >> 4;
        const int maxCy = std::min(SWTCON_GH, (PY + PH + 15) >> 4);

        // Active curve frontier: onPenMove tessellates up to m1 = midpoint(p1, p2), and the next sample
        // starts stamping at m1 and advances toward p2. Measure distance to the m1..p2 frontier so a
        // 16x16 cell is never flushed mid-dab before the curve finishes crossing it.
        int p2Sx = PX + (int)(p2.x + 0.5f);
        int p2Sy = PY + (int)(p2.y + 0.5f);
        int m1Sx = PX + (int)((p1.x + p2.x) * 0.5f + 0.5f);
        int m1Sy = PY + (int)((p1.y + p2.y) * 0.5f + 0.5f);
        int midSx = (m1Sx + p2Sx) >> 1;
        int midSy = (m1Sy + p2Sy) >> 1;

        auto ptDistToCellPx = [](int sx, int sy, int cx, int cy) -> int {
            int x0 = cx * 16, x1 = x0 + 15;
            int y0 = cy * 16, y1 = y0 + 15;
            int dx = (sx < x0) ? (x0 - sx) : ((sx > x1) ? (sx - x1) : 0);
            int dy = (sy < y0) ? (y0 - sy) : ((sy > y1) ? (sy - y1) : 0);
            return std::max(dx, dy);
        };

        auto cellDistToFrontierPx = [&](int cx, int cy) -> int {
            int d0 = ptDistToCellPx(p2Sx, p2Sy, cx, cy);
            int d1 = ptDistToCellPx(m1Sx, m1Sy, cx, cy);
            int d2 = ptDistToCellPx(midSx, midSy, cx, cy);
            return std::min(d0, std::min(d1, d2));
        };

        const int frontierHoldPx = lastDabBoundPx + 6;
        auto isBaseReadyMono = [&](int cx, int cy) -> bool {
            const SwtconCell &cell = swtconGrid[cy][cx];
            if (!cell.dirtyMono) return false;
            if (now < cell.pipeBusyUntilSec) return false;
            if (!penDown) return true;
            if (cellDistToFrontierPx(cx, cy) > frontierHoldPx) return true;
            if ((now - lastSampleTime) >= 0.032 && (now - cell.lastPenTouchSec) >= 0.032) return true;
            return false;
        };

        // Compute the rectangular bounding box U = [ux0..ux1, uy0..uy1) around the active pen-tip
        // frontier (m1..p2 ± frontierHoldPx) plus any unflushed dirty cells held back at the pen tip,
        // expanded by 2 cells perpendicular to the hysteresis-stabilized stroke direction (horizDominant).
        // Because U spans the full cross-section of the stroke ribbon, ready cells behind U are separated
        // along a clean perpendicular slice and never form an L-shaped corner trap around U!
        int ux0 = 100000, uy0 = 100000, ux1 = -100000, uy1 = -100000;
        if (penDown && (now - lastSampleTime) < 0.032) {
            int fx0 = std::min(m1Sx, p2Sx) - frontierHoldPx;
            int fx1 = std::max(m1Sx, p2Sx) + frontierHoldPx;
            int fy0 = std::min(m1Sy, p2Sy) - frontierHoldPx;
            int fy1 = std::max(m1Sy, p2Sy) + frontierHoldPx;
            ux0 = std::max(minCx, fx0 >> 4);
            ux1 = std::min(maxCx, (fx1 + 15) >> 4);
            uy0 = std::max(minCy, fy0 >> 4);
            uy1 = std::min(maxCy, (fy1 + 15) >> 4);

            for (int cy = minCy; cy < maxCy; cy++) {
                for (int cx = minCx; cx < maxCx; cx++) {
                    const SwtconCell &cell = swtconGrid[cy][cx];
                    if (cell.dirtyMono && now >= cell.pipeBusyUntilSec && !isBaseReadyMono(cx, cy)) {
                        if (cx < ux0) ux0 = cx;
                        if (cx + 1 > ux1) ux1 = cx + 1;
                        if (cy < uy0) uy0 = cy;
                        if (cy + 1 > uy1) uy1 = cy + 1;
                    }
                }
            }

            if (horizDominant) {
                uy0 = std::max(minCy, uy0 - 2);
                uy1 = std::min(maxCy, uy1 + 2);
            } else {
                ux0 = std::max(minCx, ux0 - 2);
                ux1 = std::min(maxCx, ux1 + 2);
            }
        }

        auto isInUnreadyTipBox = [&](int cx, int cy) -> bool {
            return (cx >= ux0 && cx < ux1 && cy >= uy0 && cy < uy1);
        };

        // If a dirtyMono cell is within ±2 cells of a same-pass dirtyMono neighbor that had to wait
        // on an earlier hardware pipe (e.g. at a self-crossing turnaround), align their ready time to
        // the latest pipeBusyUntilSec in that neighborhood so the entire self-crossing patch flushes
        // in one atomic batch instead of fragmenting into multiple 1x1 batches.
        auto effectiveMonoReadySec = [&](int cx, int cy) -> double {
            const SwtconCell &cell = swtconGrid[cy][cx];
            double rSec = std::max(cell.firstDirtyMonoSec, cell.pipeBusyUntilSec);
            int ny0 = std::max(minCy, cy - 4), ny1 = std::min(maxCy, cy + 5);
            int nx0 = std::max(minCx, cx - 4), nx1 = std::min(maxCx, cx + 5);
            for (int ny = ny0; ny < ny1; ny++) {
                for (int nx = nx0; nx < nx1; nx++) {
                    const SwtconCell &nb = swtconGrid[ny][nx];
                    if (nb.dirtyMono && nb.pipeBusyUntilSec > rSec &&
                        std::fabs(nb.lastPenTouchSec - cell.lastPenTouchSec) < 0.080) {
                        rSec = nb.pipeBusyUntilSec;
                    }
                }
            }
            return rSec;
        };

        auto isReadyMono = [&](int cx, int cy) -> bool {
            if (!isBaseReadyMono(cx, cy)) return false;
            if (isInUnreadyTipBox(cx, cy)) return false;
            if (now < effectiveMonoReadySec(cx, cy)) return false;
            return true;
        };

        auto isReadyColor = [&](int cx, int cy) -> bool {
            const SwtconCell &cell = swtconGrid[cy][cx];
            if (!cell.hasUnsettledColor) return false;
            if (cell.dirtyMono) return false;
            if (now < cell.pipeBusyUntilSec + 0.005) return false;
            if (isInUnreadyTipBox(cx, cy)) return false;
            if (penDown && (now - lastSampleTime) < 0.032 && cellDistToFrontierPx(cx, cy) <= frontierHoldPx) {
                return false;
            }
            return true;
        };

        int monoSeedX = -1, monoSeedY = -1;
        double oldestMonoSec = 1e30;
        int colorSeedX = -1, colorSeedY = -1;
        uint32_t oldestColorBatch = 0xFFFFFFFFu;
        double oldestColorSec = 1e30;
        uint32_t minMonoPipeBatch = 0xFFFFFFFFu;
        bool hasPendingMonoPipe = penDown;

        for (int cy = minCy; cy < maxCy; cy++) {
            for (int cx = minCx; cx < maxCx; cx++) {
                const SwtconCell &cell = swtconGrid[cy][cx];
                if (cell.dirtyMono || (cell.hasUnsettledColor && !isReadyColor(cx, cy))) {
                    hasPendingMonoPipe = true;
                }
                // Only block FIFO color progression for batches that have ALREADY been dispatched by
                // doMono (!cell.dirtyMono && monoBatchId > 0) and are still waiting on their 155ms mono pipe.
                if (cell.hasUnsettledColor && !cell.dirtyMono && cell.monoBatchId > 0 &&
                    now < cell.pipeBusyUntilSec + 0.005) {
                    if (cell.monoBatchId < minMonoPipeBatch) {
                        minMonoPipeBatch = cell.monoBatchId;
                    }
                }
                if (isReadyMono(cx, cy)) {
                    double readySec = effectiveMonoReadySec(cx, cy);
                    if (readySec < oldestMonoSec) {
                        oldestMonoSec = readySec;
                        monoSeedX = cx;
                        monoSeedY = cy;
                    }
                }
                if (isReadyColor(cx, cy)) {
                    uint32_t bId = (cell.monoBatchId > 0) ? cell.monoBatchId : 0xFFFFFFFEu;
                    if (bId < oldestColorBatch ||
                        (bId == oldestColorBatch && cell.firstDirtyMonoSec < oldestColorSec)) {
                        oldestColorBatch = bId;
                        oldestColorSec = cell.firstDirtyMonoSec;
                        colorSeedX = cx;
                        colorSeedY = cy;
                    }
                }
            }
        }

        // Strict FIFO wave ordering along the stroke: never convert a later batch while an earlier
        // mono-dispatched batch is still in its 155ms monochrome waveform pipe.
        if (hasPendingMonoPipe && colorSeedX >= 0 && oldestColorBatch > minMonoPipeBatch) {
            colorSeedX = -1;
            colorSeedY = -1;
        }

        if (monoSeedX < 0 && colorSeedX < 0) {
            return;
        }

        bool doMono = false;
        if (monoSeedX >= 0 && colorSeedX >= 0) {
            // Strict 1:1 alternation between monochrome pen-tip updates and trailing color-settle
            // updates so the color accuracy conversion wave advances at the exact same speed as the pen!
            doMono = (consecutiveMonoDispatches == 0);
        } else if (monoSeedX >= 0) {
            doMono = true;
        } else {
            doMono = false;
        }

        if (doMono) {
            consecutiveMonoDispatches++;
            consecutiveColorDispatches = 0;
            const int maxSpan = 10; // 160x160 px max coalesced mono block

            auto canIncludeMono = [&](int x, int y) -> bool {
                if (x < minCx || x >= maxCx || y < minCy || y >= maxCy) return false;
                if (isInUnreadyTipBox(x, y)) return false;
                const SwtconCell &cell = swtconGrid[y][x];
                if (now < cell.pipeBusyUntilSec) return false;
                if (cell.dirtyMono) {
                    return isReadyMono(x, y);
                }
                if (cell.hasUnsettledColor || cell.hasColorInFb) return false;
                if (penDown && (now - lastSampleTime) < 0.032 && cellDistToFrontierPx(x, y) <= frontierHoldPx) {
                    return false;
                }
                return true;
            };

            // Expand in chronological ready order from the oldest ready cell (monoSeedX, monoSeedY),
            // allowing contiguous ready dabs (within 80ms) to coalesce in best-cost order.
            int cx0 = monoSeedX, cx1 = monoSeedX + 1;
            int cy0 = monoSeedY, cy1 = monoSeedY + 1;
            while (true) {
                double minUnaddedSec = 1e30;
                int syMin = std::max(minCy, cy1 - maxSpan);
                int syMax = std::min(maxCy, cy0 + maxSpan);
                int sxMin = std::max(minCx, cx1 - maxSpan);
                int sxMax = std::min(maxCx, cx0 + maxSpan);
                for (int ty = minCy; ty < maxCy; ty++) {
                    for (int tx = minCx; tx < maxCx; tx++) {
                        if (tx >= cx0 && tx < cx1 && ty >= cy0 && ty < cy1) continue;
                        if (isReadyMono(tx, ty)) {
                            double rSec = effectiveMonoReadySec(tx, ty);
                            if (rSec < minUnaddedSec) {
                                minUnaddedSec = rSec;
                            }
                        }
                    }
                }
                if (minUnaddedSec > 1e20) break;

                int bestTx = -1, bestTy = -1;
                int bestCost = 1000000;
                double bestSec = 1e30;
                for (int ty = syMin; ty < syMax; ty++) {
                    for (int tx = sxMin; tx < sxMax; tx++) {
                        if (tx >= cx0 && tx < cx1 && ty >= cy0 && ty < cy1) continue;
                        if (!isReadyMono(tx, ty)) continue;
                        double tSec = effectiveMonoReadySec(tx, ty);
                        if (tSec > minUnaddedSec + 0.080) continue;
                        int ncx0 = std::min(cx0, tx);
                        int ncx1 = std::max(cx1, tx + 1);
                        int ncy0 = std::min(cy0, ty);
                        int ncy1 = std::max(cy1, ty + 1);
                        if ((ncx1 - ncx0) > maxSpan || (ncy1 - ncy0) > maxSpan) continue;
                        int cost = (ncx1 - ncx0) * (ncy1 - ncy0) - (cx1 - cx0) * (cy1 - cy0);
                        if (cost > bestCost || (cost == bestCost && tSec >= bestSec)) continue;
                        bool ok = true;
                        for (int y = ncy0; y < ncy1 && ok; y++) {
                            for (int x = ncx0; x < ncx1; x++) {
                                if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) continue;
                                if (!canIncludeMono(x, y)) { ok = false; break; }
                            }
                        }
                        if (ok) {
                            bestCost = cost;
                            bestSec = tSec;
                            bestTx = tx;
                            bestTy = ty;
                        }
                    }
                }
                if (bestTx < 0) break;
                cx0 = std::min(cx0, bestTx);
                cx1 = std::max(cx1, bestTx + 1);
                cy0 = std::min(cy0, bestTy);
                cy1 = std::max(cy1, bestTy + 1);
            }

            uint32_t batchId = nextMonoBatchId++;
            const uint32_t pureBlack = rgba(0, 0, 0);
            const uint32_t pureWhite = rgba(255, 255, 255);
            for (int y = cy0; y < cy1; y++) {
                for (int x = cx0; x < cx1; x++) {
                    SwtconCell &cell = swtconGrid[y][x];
                    if (!cell.dirtyMono) {
                        continue;
                    }
                    int sy0 = std::max(PY, y * 16);
                    int sy1 = std::min(PY + PH, (y + 1) * 16);
                    int sx0 = std::max(PX, x * 16);
                    int sx1 = std::min(PX + PW, (x + 1) * 16);
                    for (int sy = sy0; sy < sy1; sy++) {
                        int ly = sy - PY;
                        uint32_t *fbRow = &c.fb[sy * c.W];
                        const uint32_t *pRow = &paperBuf[ly * PW];
                        uint8_t *stRow = &colorSettleState[ly * PW];
                        for (int sx = sx0; sx < sx1; sx++) {
                            int lx = sx - PX;
                            uint8_t st = stRow[lx];
                            if (st == 2) {
                                fbRow[sx] = pureBlack;
                                stRow[lx] = 1;
                                cell.hasUnsettledColor = true;
                            } else if (st == 3) {
                                fbRow[sx] = pureWhite;
                                stRow[lx] = 1;
                                cell.hasUnsettledColor = true;
                            } else if (st == 4) {
                                fbRow[sx] = pRow[lx];
                                stRow[lx] = 0;
                            } else if (cell.hasColorInFb || cell.hasColorPixels) {
                                uint32_t px = fbRow[sx];
                                if (px != pureBlack && px != pureWhite) {
                                    fbRow[sx] = pureBlack;
                                    stRow[lx] = 1;
                                    cell.hasUnsettledColor = true;
                                }
                            }
                        }
                    }
                    cell.lastMonoStrokeId = strokeId;
                    if (cell.hasUnsettledColor) {
                        cell.monoBatchId = batchId;
                    }
                    cell.hasColorInFb = false;
                    cell.dirtyMono = false;
                    // Only mark cells that actually had dirtyMono ink updated as busy! Empty white paper
                    // cells inside [cx0..cx1, cy0..cy1) have m_mask == 0 in swtcon and are not in [0x38..0x44].
                    cell.pipeBusyUntilSec = now + 0.155;
                }
            }

            int bx0 = std::max(0, cx0 * 16);
            int by0 = std::max(0, cy0 * 16);
            int bx1 = std::min(c.W, cx1 * 16);
            int by1 = std::min(c.H, cy1 * 16);
            // Counteract FBController::markedUpdate's +1 on (x2, y2) (0x2bd10..0x2bd30) so the QRect
            // passed to QtQuick/swtcon is exact [bx0..bx1-1, by0..by1-1] and never bleeds into (cx1, cy1).
            conn.updateRegion(bx0, by0, std::max(1, bx1 - bx0 - 1), std::max(1, by1 - by0 - 1));
            lastSwtconDispatchSec = now;
            lastDispatchWasColor = false;
            lastInkFlushSec = now;
            return;
        }

        consecutiveMonoDispatches = 0;
        consecutiveColorDispatches++;
        int cx0 = colorSeedX, cx1 = colorSeedX + 1;
        int cy0 = colorSeedY, cy1 = colorSeedY + 1;

        const int maxSpan = hasPendingMonoPipe ? 12 : 36;

        auto canIncludeColor = [&](int x, int y) -> bool {
            if (x < minCx || x >= maxCx || y < minCy || y >= maxCy) return false;
            if (isInUnreadyTipBox(x, y)) return false;
            const SwtconCell &cell = swtconGrid[y][x];
            if (now < cell.pipeBusyUntilSec) return false;
            if (cell.dirtyMono) return false;
            if (cell.hasUnsettledColor) {
                return isReadyColor(x, y);
            }
            if (penDown && (now - lastSampleTime) < 0.032 && cellDistToFrontierPx(x, y) <= frontierHoldPx) {
                return false;
            }
            return true;
        };

        // Step 1: Include all ready cells of oldestColorBatch into [cx0..cx1, cy0..cy1).
        for (int ty = minCy; ty < maxCy; ty++) {
            for (int tx = minCx; tx < maxCx; tx++) {
                if (tx >= cx0 && tx < cx1 && ty >= cy0 && ty < cy1) continue;
                if (!isReadyColor(tx, ty)) continue;
                const SwtconCell &cell = swtconGrid[ty][tx];
                uint32_t bId = (cell.monoBatchId > 0) ? cell.monoBatchId : 0xFFFFFFFEu;
                if (bId != oldestColorBatch) continue;
                int ncx0 = std::min(cx0, tx);
                int ncx1 = std::max(cx1, tx + 1);
                int ncy0 = std::min(cy0, ty);
                int ncy1 = std::max(cy1, ty + 1);
                if ((ncx1 - ncx0) > maxSpan || (ncy1 - ncy0) > maxSpan) continue;
                bool ok = true;
                for (int y = ncy0; y < ncy1 && ok; y++) {
                    for (int x = ncx0; x < ncx1; x++) {
                        if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) continue;
                        if (!canIncludeColor(x, y)) { ok = false; break; }
                    }
                }
                if (ok) {
                    cx0 = ncx0;
                    cx1 = ncx1;
                    cy0 = ncy0;
                    cy1 = ncy1;
                }
            }
        }

        // Step 2: Batch-atomic merging of subsequent ready batches (nextBatch).
        // Only merge nextBatch if 100% of nextBatch's cells are ready and fit within maxSpan without
        // violating canIncludeColor, so a monoBatchId is never split across two 560ms color pipes!
        while (true) {
            uint32_t nextBatch = 0xFFFFFFFFu;
            for (int ty = minCy; ty < maxCy; ty++) {
                for (int tx = minCx; tx < maxCx; tx++) {
                    if (tx >= cx0 && tx < cx1 && ty >= cy0 && ty < cy1) continue;
                    const SwtconCell &cell = swtconGrid[ty][tx];
                    if (cell.hasUnsettledColor && !cell.dirtyMono) {
                        uint32_t bId = (cell.monoBatchId > 0) ? cell.monoBatchId : 0xFFFFFFFEu;
                        if (bId < nextBatch) {
                            nextBatch = bId;
                        }
                    }
                }
            }
            if (nextBatch == 0xFFFFFFFFu || nextBatch == oldestColorBatch) break;
            if (hasPendingMonoPipe && nextBatch > minMonoPipeBatch) break;

            int ncx0 = cx0, ncx1 = cx1, ncy0 = cy0, ncy1 = cy1;
            bool batchAllReady = true;
            for (int ty = minCy; ty < maxCy && batchAllReady; ty++) {
                for (int tx = minCx; tx < maxCx; tx++) {
                    const SwtconCell &cell = swtconGrid[ty][tx];
                    if (!cell.hasUnsettledColor) continue;
                    uint32_t bId = (!cell.dirtyMono && cell.monoBatchId > 0) ? cell.monoBatchId : 0xFFFFFFFEu;
                    if (bId == nextBatch) {
                        if (!isReadyColor(tx, ty)) {
                            batchAllReady = false;
                            break;
                        }
                        ncx0 = std::min(ncx0, tx);
                        ncx1 = std::max(ncx1, tx + 1);
                        ncy0 = std::min(ncy0, ty);
                        ncy1 = std::max(ncy1, ty + 1);
                    }
                }
            }
            if (!batchAllReady) break;
            if ((ncx1 - ncx0) > maxSpan || (ncy1 - ncy0) > maxSpan) break;

            bool ok = true;
            for (int y = ncy0; y < ncy1 && ok; y++) {
                for (int x = ncx0; x < ncx1; x++) {
                    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) continue;
                    if (!canIncludeColor(x, y)) { ok = false; break; }
                }
            }
            if (!ok) break;
            cx0 = ncx0;
            cx1 = ncx1;
            cy0 = ncy0;
            cy1 = ncy1;
        }

        for (int y = cy0; y < cy1; y++) {
            for (int x = cx0; x < cx1; x++) {
                SwtconCell &cell = swtconGrid[y][x];
                if (cell.hasUnsettledColor) {
                    int sy0 = std::max(PY, y * 16);
                    int sy1 = std::min(PY + PH, (y + 1) * 16);
                    int sx0 = std::max(PX, x * 16);
                    int sx1 = std::min(PX + PW, (x + 1) * 16);
                    for (int sy = sy0; sy < sy1; sy++) {
                        int ly = sy - PY;
                        uint8_t *stRow = &colorSettleState[ly * PW];
                        const uint32_t *pRow = &paperBuf[ly * PW];
                        uint32_t *fbRow = &c.fb[sy * c.W];
                        for (int sx = sx0; sx < sx1; sx++) {
                            int lx = sx - PX;
                            if (stRow[lx] != 0) {
                                fbRow[sx] = pRow[lx];
                                stRow[lx] = 0;
                            }
                        }
                    }
                    cell.hasUnsettledColor = false;
                    cell.monoBatchId = 0;
                    cell.hasColorInFb = cell.hasColorPixels;
                    // Only mark cells that actually had unsettled color pixels converted as busy!
                    cell.pipeBusyUntilSec = now + 0.560;
                }
            }
        }

        int bx0 = std::max(0, cx0 * 16);
        int by0 = std::max(0, cy0 * 16);
        int bx1 = std::min(c.W, cx1 * 16);
        int by1 = std::min(c.H, cy1 * 16);
        conn.updateRegion(bx0, by0, std::max(1, bx1 - bx0 - 1), std::max(1, by1 - by0 - 1));
        lastSwtconDispatchSec = now;
        lastDispatchWasColor = true;
        lastInkFlushSec = now;
    }

    // Flush accumulated pen-tip dirty bounding box without ever calling setRefreshMode.
    inline void flushInkDirty(Canvas2D &c, qtfb::Connection &conn) {
        curRefreshMode = conn.currentRefreshMode;
        if (curRefreshMode == REFRESH_MODE_CONTENT || curRefreshMode == REFRESH_MODE_UI) {
            resetDirtyRect();
            stepSwtconPipeline(c, conn);
            return;
        }
        if (dirtyX1 > dirtyX0 && dirtyY1 > dirtyY0) {
            conn.updateRegion(dirtyX0, dirtyY0, dirtyX1 - dirtyX0, dirtyY1 - dirtyY0);
            resetDirtyRect();
            lastInkFlushSec = inkNowSec();
        }
    }

    // --- Public Pen Event API ---
    void onPenDown(Canvas2D &c, qtfb::Connection &conn, float sx, float sy, float pressure,
                   const RawMarkerState &marker, bool deferFlush = false) {
        double t0 = inkNowSec();
        curRefreshMode = conn.currentRefreshMode;
        bool isEraser = marker.toolRubber || (activeTool == TOOL_ERASER);
        activeStrokeIsEraser = isEraser;

        penDown = true;
        predTip.active = false;
        strokeId++;
        curHiliteId = (uint16_t)((curHiliteId % 65000u) + 1u);

        beginUndoCapture();
        if (!deferFlush) resetDirtyRect();

        float lx = std::clamp(sx - (float)PX, 0.0f, (float)(PW - 1));
        float ly = std::clamp(sy - (float)PY, 0.0f, (float)(PH - 1));
        float normP = std::clamp(pressure, 0.05f, 1.0f);

        livePressure = normP;
        liveRawPressure = (marker.rawPressure > 0) ? marker.rawPressure : (int)(normP * 4095.0f);
        updateTiltMetrics(marker.tiltX, marker.tiltY, -0.68f);
        hudNeedsUpdate = true;

        StrokeSample s = {lx, ly, normP, marker.tiltX, marker.tiltY, -0.68f, t0};
        p0 = p1 = p2 = s;
        strokeSampleCount = 1;
        lastStrokePts = 1;
        lastSampleTime = t0;
        curStrokeSpeed = 0.0f;
        smoothDx = 1.0f;
        smoothDy = 0.0f;
        horizDominant = true;

        hasErasedPixels = false;
        stampDab(c, lx, ly, normP, marker.tiltX, marker.tiltY, -0.68f, isEraser, false);

        if (!deferFlush) {
            flushInkDirty(c, conn);
        }
        dispatchUs = (int)((inkNowSec() - t0) * 1e6);
    }

    void onPenMove(Canvas2D &c, qtfb::Connection &conn, float sx, float sy, float pressure,
                   const RawMarkerState &marker, bool deferFlush = false) {
        if (!penDown) {
            onPenDown(c, conn, sx, sy, pressure, marker, deferFlush);
            return;
        }
        double t0 = inkNowSec();
        curRefreshMode = conn.currentRefreshMode;
        float lx = std::clamp(sx - (float)PX, 0.0f, (float)(PW - 1));
        float ly = std::clamp(sy - (float)PY, 0.0f, (float)(PH - 1));
        float normP = std::clamp(pressure, 0.05f, 1.0f);

        float dx = lx - p2.x;
        float dy = ly - p2.y;
        float moveDist = hypotf(dx, dy);
        if (moveDist < 0.55f && fabsf(normP - p2.pressure) < 0.02f) {
            if (abs(marker.tiltX - liveTiltX) >= 50 || abs(marker.tiltY - liveTiltY) >= 50 ||
                abs(marker.rawPressure - liveRawPressure) >= 35) {
                livePressure = normP;
                liveRawPressure = (marker.rawPressure > 0) ? marker.rawPressure : (int)(normP * 4095.0f);
                updateTiltMetrics(marker.tiltX, marker.tiltY, p2.dirRad);
                hudNeedsUpdate = true;
            }
            return;
        }

        float dirRad = (moveDist > 0.5f) ? atan2f(dy, dx) : p2.dirRad;
        curStrokeSpeed = curStrokeSpeed * 0.72f + moveDist * 0.28f;
        smoothDx = smoothDx * 0.75f + dx * 0.25f;
        smoothDy = smoothDy * 0.75f + dy * 0.25f;
        if (horizDominant) {
            if (fabsf(smoothDy) > fabsf(smoothDx) * 1.35f) horizDominant = false;
        } else {
            if (fabsf(smoothDx) > fabsf(smoothDy) * 1.35f) horizDominant = true;
        }
        livePressure = normP;
        liveRawPressure = (marker.rawPressure > 0) ? marker.rawPressure : (int)(normP * 4095.0f);
        updateTiltMetrics(marker.tiltX, marker.tiltY, dirRad);
        hudNeedsUpdate = true;

        // Update live digitizer sample rate EMA
        double dt = t0 - lastSampleTime;
        lastSampleTime = t0;
        if (dt > 0.0004 && dt < 0.20) {
            float instHz = (float)(1.0 / dt);
            samplesPerSec = (samplesPerSec < 10.0f) ? instHz : (samplesPerSec * 0.82f + instHz * 0.18f);
        }

        if (!deferFlush) {
            resetDirtyRect();
        }

        // 1. Restore any transient velocity-predicted tip from paperBuf before drawing committed ink
        restorePredictedTip(c);

        // 2. Shift sample window and tessellate Midpoint Quadratic Bezier curve into paperBuf + c.fb
        p0 = p1;
        p1 = p2;
        p2 = {lx, ly, normP, marker.tiltX, marker.tiltY, dirRad, t0};
        strokeSampleCount++;
        lastStrokePts = strokeSampleCount;

        bool isEraser = marker.toolRubber || (activeTool == TOOL_ERASER);
        if (isEraser) activeStrokeIsEraser = true;
        StrokeSample m1 = midpointSample(p1, p2);

        if (strokeSampleCount == 2) {
            StrokeSample m01 = midpointSample(p1, m1);
            tessellateQuadraticBezier(c, p1, m01, m1, isEraser, false);
        } else {
            StrokeSample m0 = midpointSample(p0, p1);
            tessellateQuadraticBezier(c, m0, p1, m1, isEraser, false);
        }

        // 3. Transient leading tip (only in stateless UFAST/FAST/ANIMATE modes) or timed eraser trail step
        if (!isEraser) {
            if (curRefreshMode != REFRESH_MODE_CONTENT && curRefreshMode != REFRESH_MODE_UI) {
                StrokeSample tipEnd = p2;
                if (predictEnabled && moveDist >= 3.2f) {
                    float leadScale = std::min(0.65f, moveDist * 0.04f);
                    tipEnd.x = std::clamp(p2.x + (p2.x - p1.x) * leadScale, 0.0f, (float)(PW - 1));
                    tipEnd.y = std::clamp(p2.y + (p2.y - p1.y) * leadScale, 0.0f, (float)(PH - 1));
                }
                tessellateQuadraticBezier(c, m1, p2, tipEnd, false, true);
            }
        } else {
            StrokeSample mMid = midpointSample(m1, p2);
            tessellateQuadraticBezier(c, m1, mMid, p2, true, false);
        }

        if (!deferFlush) {
            flushInkDirty(c, conn);
        }
        dispatchUs = (int)((inkNowSec() - t0) * 1e6);
    }

    void onPenUp(Canvas2D &c, qtfb::Connection &conn, const RawMarkerState &marker, bool deferFlush = false) {
        if (!penDown) return;
        double t0 = inkNowSec();
        curRefreshMode = conn.currentRefreshMode;
        penDown = false;
        hudNeedsUpdate = true;

        if (!deferFlush) {
            resetDirtyRect();
        }

        restorePredictedTip(c);

        bool isEraser = activeStrokeIsEraser || marker.toolRubber || (activeTool == TOOL_ERASER);
        if (strokeSampleCount >= 2) {
            StrokeSample m1 = midpointSample(p1, p2);
            StrokeSample mMid = midpointSample(m1, p2);
            tessellateQuadraticBezier(c, m1, mMid, p2, isEraser, false);
        }
        if (isEraser) {
            settleEraserStroke(c);
        }
        activeStrokeIsEraser = false;
        flushActiveSettleSegment(0.055);

        commitUndoCapture();

        if (!deferFlush) {
            flushInkDirty(c, conn);
        }
        dispatchUs = (int)((inkNowSec() - t0) * 1e6);
    }

    // Non-Colliding Single-Pass Color Settling & Deferred Monochrome Swtcon Scheduler
    void pollColorSettle(Canvas2D &c, qtfb::Connection &conn) {
        curRefreshMode = conn.currentRefreshMode;
        if (curRefreshMode != REFRESH_MODE_CONTENT && curRefreshMode != REFRESH_MODE_UI) {
            if (hasPendingSwtconWork()) {
                // Mode switched away from CONTENT/UI mid-settle: commit pending ink in place (no full refresh)
                int fx0 = PW, fy0 = PH, fx1 = 0, fy1 = 0;
                for (int ly = 0; ly < PH; ly++) {
                    uint8_t *stRow = &colorSettleState[ly * PW];
                    const uint32_t *pRow = &paperBuf[ly * PW];
                    uint32_t *fbRow = &c.fb[(PY + ly) * c.W + PX];
                    for (int lx = 0; lx < PW; lx++) {
                        if (stRow[lx] != 0) {
                            fbRow[lx] = pRow[lx];
                            stRow[lx] = 0;
                            if (lx < fx0) fx0 = lx;
                            if (lx + 1 > fx1) fx1 = lx + 1;
                            if (ly < fy0) fy0 = ly;
                            if (ly + 1 > fy1) fy1 = ly + 1;
                        }
                    }
                }
                for (int cy = 0; cy < SWTCON_GH; cy++) {
                    for (int cx = 0; cx < SWTCON_GW; cx++) {
                        swtconGrid[cy][cx].dirtyMono = false;
                        swtconGrid[cy][cx].hasUnsettledColor = false;
                    }
                }
                if (fx1 > fx0 && fy1 > fy0) {
                    conn.updateRegion(PX + fx0, PY + fy0, fx1 - fx0, fy1 - fy0);
                }
            }
            return;
        }

        stepSwtconPipeline(c, conn);
    }

    // Non-interfering compact HUD updates
    void pollHudUpdate(Canvas2D &c, qtfb::Connection &conn, const RawMarkerState &marker) {
        // Flush compact HUD pill ONLY when showStatsHud is ON and all active stroke/settle work is idle
        if (!showStatsHud || !hudNeedsUpdate) return;
        if (penDown || hasPendingSwtconWork()) return;
        double now = inkNowSec();
        if ((now - lastInkFlushSec) < 0.095) return;
        if ((now - lastSwtconDispatchSec) < 0.060) return;
        if ((now - lastHudUpdateSec) < 0.110) return;
        hudNeedsUpdate = false;
        lastHudUpdateSec = now;
        lastSwtconDispatchSec = now;
        drawMarkerHud(c, marker);
        conn.updateRegion(HX, HY, HW, HH);
    }

    // Handle toolbar button taps; returns true if a toolbar control was tapped
    bool handleTap(Canvas2D &c, qtfb::Connection &conn, int x, int y, const RawMarkerState &marker) {
        if (y < TY || y >= PY) return false;

        bool changedToolbar = false;
        bool changedPaper   = false;

        for (int i = 0; i < 6; i++) {
            if (btnTools[i].contains(x, y)) {
                activeTool = i;
                changedToolbar = true;
                break;
            }
        }

        if (!changedToolbar) {
            for (int i = 0; i < 6; i++) {
                if (btnColors[i].contains(x, y)) {
                    activeColor = i;
                    if (activeTool == TOOL_ERASER) activeTool = TOOL_FINELINER;
                    changedToolbar = true;
                    break;
                }
            }
        }

        if (!changedToolbar) {
            for (int i = 0; i < 3; i++) {
                if (btnSizes[i].contains(x, y)) {
                    activeSize = i;
                    changedToolbar = true;
                    break;
                }
            }
        }

        if (!changedToolbar) {
            for (int i = 0; i < 3; i++) {
                if (btnTemplates[i].contains(x, y)) {
                    if (activeTemplate != i) {
                        activeTemplate = i;
                        resetPaperToTemplate();
                        changedPaper = true;
                    }
                    changedToolbar = true;
                    break;
                }
            }
        }

        if (!changedToolbar && btnPredict.contains(x, y)) {
            predictEnabled = !predictEnabled;
            changedToolbar = true;
        }

        if (!changedToolbar && btnStats.contains(x, y)) {
            showStatsHud = !showStatsHud;
            changedToolbar = true;
        }

        if (!changedToolbar && btnUndo.contains(x, y)) {
            performUndo(c, conn, marker);
            return true;
        }

        if (!changedToolbar && btnClear.contains(x, y)) {
            beginUndoCapture();
            for (int ty = 0; ty < TILES_Y; ty++) {
                for (int tx = 0; tx < TILES_X; tx++) {
                    int x0 = tx * TILE_SZ, y0 = ty * TILE_SZ;
                    int w = std::min(TILE_SZ, PW - x0), h = std::min(TILE_SZ, PH - y0);
                    bool hasInk = false;
                    for (int r = 0; r < h && !hasInk; r += 2) {
                        for (int col = 0; col < w; col += 2) {
                            if (paperBuf[(y0 + r) * PW + (x0 + col)] != templatePixelAt(x0 + col, y0 + r)) {
                                hasInk = true;
                                break;
                            }
                        }
                    }
                    if (hasInk) ensureTileSaved(x0, y0);
                }
            }
            commitUndoCapture();
            resetPaperToTemplate();
            changedToolbar = true;
            changedPaper = true;
        }

        if (changedPaper) {
            drawPage(c, marker);
            conn.updateRegion(TX, TY, TW, 2140 - TY);
            lastSwtconDispatchSec = inkNowSec();
            return true;
        }

        if (changedToolbar) {
            drawToolbar(c, marker);
            conn.updateRegion(TX, TY, TW, TH);
            lastSwtconDispatchSec = inkNowSec();
            return true;
        }

        return false;
    }

    // Draw the Compact Stylus Stats Cluster (388x54 pill inside Row 2 of Toolbar at HX=1196, HY=250)
    void drawMarkerHud(Canvas2D &c, const RawMarkerState &marker) {
        (void)marker;
        if (!showStatsHud) {
            // When hidden, fill with clean toolbar background
            c.fillRect(HX, HY, HW, HH, rgba(232, 236, 242));
            return;
        }

        c.fillRoundRect(HX, HY, HW, HH, 9, rgba(22, 28, 40), rgba(65, 80, 105), 2);

        // --- 1. Left Gauge: Compact Circular Tip Pressure Icon (matching Tilt Vector icon!) ---
        int pCx = HX + 24, pCy = HY + HH / 2, pR = 17;
        c.fillCircle(pCx, pCy, pR, rgba(14, 18, 26));
        c.drawCircle(pCx, pCy, pR, 2, rgba(110, 135, 175));
        // Subtle 50% reference ring & crosshairs
        c.drawCircle(pCx, pCy, 9, 1, rgba(50, 62, 82));
        c.fillRect(pCx - pR + 3, pCy, pR * 2 - 6, 1, rgba(45, 56, 75));
        c.fillRect(pCx, pCy - pR + 3, 1, pR * 2 - 6, rgba(45, 56, 75));

        int pct = std::clamp((liveRawPressure * 100) / 4095, 0, 100);
        uint32_t pCol = (pct > 75) ? rgba(255, 145, 35) : rgba(45, 215, 115);
        int fillRad = (liveRawPressure > 0) ? std::clamp(2 + (liveRawPressure * (pR - 4)) / 4095, 2, pR - 2) : 0;
        if (fillRad > 0) {
            c.fillCircle(pCx, pCy, fillRad, pCol);
        }

        // Compact 11px Pressure & Rate text next to Pressure Icon
        int tx1 = pCx + pR + 7;
        char pLine1[32], pLine2[32], pLine3[32];
        snprintf(pLine1, sizeof(pLine1), "PRESS: %d%%", pct);
        snprintf(pLine2, sizeof(pLine2), "%d / 4095", liveRawPressure);
        snprintf(pLine3, sizeof(pLine3), "%.0fHz %dus", samplesPerSec, dispatchUs);
        c.drawCompactText(tx1, HY + 6,  pLine1, rgba(255, 255, 255), true, 122);
        c.drawCompactText(tx1, HY + 21, pLine2, pCol, false, 122);
        c.drawCompactText(tx1, HY + 36, pLine3, rgba(165, 185, 210), false, 122);

        // --- 2. Right Gauge: Compact 2D Tilt Vector & Chisel Nib Icon ---
        int radCx = HX + 194, radCy = HY + HH / 2, radR = 17;
        c.fillCircle(radCx, radCy, radR, rgba(14, 18, 26));
        c.drawCircle(radCx, radCy, radR, 2, rgba(110, 135, 175));
        c.fillRect(radCx - radR + 3, radCy, radR * 2 - 6, 1, rgba(45, 56, 75));
        c.fillRect(radCx, radCy - radR + 3, 1, radR * 2 - 6, rgba(45, 56, 75));

        // Draw Chisel Nib bar inside the radar at current nib angle
        float nibAngle = -0.68f, tiltNorm = 0.0f;
        computeNibOrientation(liveTiltX, liveTiltY, -0.68f, nibAngle, tiltNorm);
        int nx = (int)(cosf(nibAngle) * 12.0f);
        int ny = (int)(sinf(nibAngle) * 12.0f);
        c.drawLine(radCx - nx, radCy - ny, radCx + nx, radCy + ny, 2, rgba(255, 215, 55));

        // Draw Tilt Vector dot/arrow
        float azRad = liveAzimuthDeg * (3.14159265f / 180.0f);
        float len = std::clamp(liveSlantDeg / 65.0f, 0.0f, 1.0f) * (radR - 4);
        int vx = radCx + (int)(cosf(azRad) * len);
        int vy = radCy + (int)(sinf(azRad) * len);
        c.drawLine(radCx, radCy, vx, vy, 2, rgba(60, 225, 130));
        c.fillCircle(vx, vy, 3, rgba(60, 225, 130));

        // Compact 11px Tilt & Angle text next to Tilt Icon
        float txDeg = (abs(liveTiltX) > 180) ? (liveTiltX / 100.0f) : (float)liveTiltX;
        float tyDeg = (abs(liveTiltY) > 180) ? (liveTiltY / 100.0f) : (float)liveTiltY;
        int tx2 = radCx + radR + 7;
        char aLine1[36], aLine2[36], aLine3[36];
        snprintf(aLine1, sizeof(aLine1), "X:%+.0f Y:%+.0f deg", txDeg, tyDeg);
        snprintf(aLine2, sizeof(aLine2), "SLANT: %.1f deg", liveSlantDeg);
        snprintf(aLine3, sizeof(aLine3), "NIB AZ: %+.0f deg", liveAzimuthDeg);
        c.drawCompactText(tx2, HY + 6,  aLine1, rgba(165, 215, 255), true, 162);
        c.drawCompactText(tx2, HY + 21, aLine2, rgba(60, 225, 130), false, 162);
        c.drawCompactText(tx2, HY + 36, aLine3, rgba(255, 215, 55), false, 162);
    }

    void drawToolbar(Canvas2D &c, const RawMarkerState &marker) {
        c.fillRoundRect(TX, TY, TW, TH, 12, rgba(232, 236, 242), rgba(30, 35, 45), 3);

        static const char *toolLabels[6] = {
            "FINELINER",
            "BALLPOINT",
            "CALLIGRAPH",
            "PENCIL",
            "HILIGHTER",
            "ERASER"
        };
        int effTool = marker.toolRubber ? TOOL_ERASER : activeTool;
        for (int i = 0; i < 6; i++) {
            const UIButton &b = btnTools[i];
            bool sel = (effTool == i);
            uint32_t fillCol = sel ? rgba(22, 92, 205) : rgba(252, 252, 250);
            uint32_t bordCol = sel ? rgba(12, 25, 50)  : rgba(70, 80, 95);
            uint32_t txtCol  = sel ? rgba(255, 255, 255) : rgba(25, 30, 40);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 10, fillCol, bordCol, sel ? 3 : 2);
            c.drawTextCentered(b.x, b.y, b.w, b.h, toolLabels[i], 2, txtCol, true);
        }

        for (int i = 0; i < 6; i++) {
            const UIButton &b = btnColors[i];
            uint8_t r, g, bl;
            getInkRgb(i, r, g, bl);
            uint32_t swCol = rgba(r, g, bl);
            bool sel = (activeColor == i);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 9, swCol,
                            sel ? rgba(15, 20, 30) : rgba(110, 118, 130), sel ? 4 : 2);
            if (sel) {
                c.fillCircle(b.x + b.w / 2, b.y + b.h / 2, 7, rgba(255, 255, 255));
                c.fillCircle(b.x + b.w / 2, b.y + b.h / 2, 3, swCol);
            }
        }

        static const char *sizeLabels[3] = {"S", "M", "L"};
        for (int i = 0; i < 3; i++) {
            const UIButton &b = btnSizes[i];
            bool sel = (activeSize == i);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 9,
                            sel ? rgba(35, 45, 65) : rgba(250, 250, 248),
                            rgba(50, 60, 75), sel ? 3 : 2);
            c.drawTextCentered(b.x, b.y, b.w, b.h, sizeLabels[i], 2,
                               sel ? rgba(255, 255, 255) : rgba(25, 30, 40), true);
        }

        static const char *tmplLabels[3] = {"RULED", "DOTS", "GRID"};
        for (int i = 0; i < 3; i++) {
            const UIButton &b = btnTemplates[i];
            bool sel = (activeTemplate == i);
            c.fillRoundRect(b.x, b.y, b.w, b.h, 9,
                            sel ? rgba(20, 130, 85) : rgba(250, 250, 248),
                            rgba(45, 55, 70), sel ? 3 : 2);
            c.drawTextCentered(b.x, b.y, b.w, b.h, tmplLabels[i], 2,
                               sel ? rgba(255, 255, 255) : rgba(25, 30, 40), true);
        }

        c.fillRoundRect(btnPredict.x, btnPredict.y, btnPredict.w, btnPredict.h, 9,
                        predictEnabled ? rgba(155, 38, 140) : rgba(245, 246, 248),
                        rgba(45, 55, 70), predictEnabled ? 3 : 2);
        c.drawTextCentered(btnPredict.x, btnPredict.y, btnPredict.w, btnPredict.h,
                           "PREDICT", 2,
                           predictEnabled ? rgba(255, 255, 255) : rgba(35, 40, 50), true);

        c.fillRoundRect(btnStats.x, btnStats.y, btnStats.w, btnStats.h, 9,
                        showStatsHud ? rgba(25, 115, 165) : rgba(245, 246, 248),
                        rgba(45, 55, 70), showStatsHud ? 3 : 2);
        c.drawTextCentered(btnStats.x, btnStats.y, btnStats.w, btnStats.h,
                           "STATS", 2,
                           showStatsHud ? rgba(255, 255, 255) : rgba(35, 40, 50), true);

        bool canUndo = (undoCount > 0);
        c.fillRoundRect(btnUndo.x, btnUndo.y, btnUndo.w, btnUndo.h, 9,
                        canUndo ? rgba(250, 250, 248) : rgba(225, 228, 232),
                        rgba(55, 65, 80), 2);
        c.drawTextCentered(btnUndo.x, btnUndo.y, btnUndo.w, btnUndo.h,
                           "UNDO", 2,
                           canUndo ? rgba(20, 25, 35) : rgba(130, 135, 145), true);

        c.fillRoundRect(btnClear.x, btnClear.y, btnClear.w, btnClear.h, 9,
                        rgba(210, 45, 50), rgba(45, 20, 25), 2);
        c.drawTextCentered(btnClear.x, btnClear.y, btnClear.w, btnClear.h,
                           "CLEAR", 2, rgba(255, 255, 255), true);

        drawMarkerHud(c, marker);
    }

    // Full-page redraw (used on tab switch, template change, or clear)
    void drawPage(Canvas2D &c, const RawMarkerState &marker) {
        drawToolbar(c, marker);

        // Fill paper margin & border with pure White (0xFFFFFFFF) and pure Black (0xFF000000)
        // so boundary 16x16 blocks around the canvas never contain gray pixels during color settling!
        c.fillRect(0, PY - 4, c.W, c.H - (PY - 4), rgba(255, 255, 255));
        c.drawRect(PX - 2, PY - 2, PW + 4, PH + 4, 2, rgba(0, 0, 0));

        for (int ly = 0; ly < PH; ly++) {
            uint32_t *dst = &c.fb[(PY + ly) * c.W + PX];
            const uint32_t *src = &paperBuf[ly * PW];
            memcpy(dst, src, PW * sizeof(uint32_t));
        }
    }
};
