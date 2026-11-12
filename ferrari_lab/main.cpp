#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <poll.h>
#include "qtfb_protocol.h"
#include "gfx2d.h"
#include "benchmarks.h"
#include "showcase.h"
#include "ink_lab.h"

static volatile bool gRunning = true;
static void handleSig(int) { gRunning = false; }

struct AppUI {
    int activeTab = 0; // 0 = Benchmark Suite, 1 = 3D/4D Showcase, 2 = Low-Latency Ink
    UIButton btnTab[3];
    UIButton btnWaveform;
    UIButton btnDeghost;
    UIButton btnExit;

    void drawHeaderAndTabs(Canvas2D &c, const HardwareTelemetry &hw, int currentWaveform) {
        // Dark Header Banner (y: 0..94)
        c.fillRect(0, 0, c.W, 94, rgba(18, 22, 32));
        c.fillRect(0, 92, c.W, 4, rgba(45, 115, 225));

        c.drawText(24, 16, "FERRARI LAB // PAPER PRO STUDIO", 3, rgba(255, 255, 255), true, 870);

        char teleStrip[128];
        snprintf(teleStrip, sizeof(teleStrip), "CPU: %.0f%% (%d MHz, %.1fC) | BATT: %.1f%% | MARKER: %d%%",
                 hw.totalCpuLoadPct, hw.cpuFreqMhz[0], hw.cpuTempC, hw.battCapacityPct, hw.markerBatteryPct);
        c.drawText(24, 56, teleStrip, 2, rgba(175, 210, 250), false, 870);

        const char *wfNames[6] = {"UFAST (COLOR)", "FAST (MONO)", "ANIMATE (B&W)", "CONTENT (HQ)", "UI (COLOR)", "SLEEP"};
        int wfIdx = std::clamp(currentWaveform, 0, 5);
        char wfLabel[48];
        snprintf(wfLabel, sizeof(wfLabel), "MODE: %s", wfNames[wfIdx]);

        btnWaveform = {910, 15, 290, 64};
        c.fillRoundRect(btnWaveform.x, btnWaveform.y, btnWaveform.w, btnWaveform.h, 12, rgba(35, 90, 185), rgba(140, 185, 255), 2);
        c.drawTextCentered(btnWaveform.x, btnWaveform.y, btnWaveform.w, btnWaveform.h, wfLabel, 2, rgba(255, 255, 255), true);

        btnDeghost = {1214, 15, 210, 64};
        c.fillRoundRect(btnDeghost.x, btnDeghost.y, btnDeghost.w, btnDeghost.h, 12, rgba(65, 75, 95), rgba(190, 200, 220), 2);
        c.drawTextCentered(btnDeghost.x, btnDeghost.y, btnDeghost.w, btnDeghost.h, "DE-GHOST", 2, rgba(255, 255, 255), true);

        btnExit = {1438, 15, 158, 64};
        c.fillRoundRect(btnExit.x, btnExit.y, btnExit.w, btnExit.h, 12, rgba(195, 40, 45), rgba(255, 180, 180), 2);
        c.drawTextCentered(btnExit.x, btnExit.y, btnExit.w, btnExit.h, "EXIT", 2, rgba(255, 255, 255), true);

        // 3-Tab Navigation Bar (y: 104..168)
        btnTab[0] = {24,   104, 512, 64};
        btnTab[1] = {554,  104, 512, 64};
        btnTab[2] = {1084, 104, 512, 64};

        const char *tabLabels[3] = {
            "1: BENCHMARK SUITE",
            "2: 3D/4D SHOWCASE",
            "3: LOW-LATENCY INK"
        };
        const uint32_t tabAccents[3] = {
            rgba(25, 95, 205),
            rgba(165, 35, 135),
            rgba(18, 135, 80)
        };

        for (int t = 0; t < 3; t++) {
            bool act = (activeTab == t);
            c.fillRoundRect(btnTab[t].x, btnTab[t].y, btnTab[t].w, btnTab[t].h, 12,
                            act ? tabAccents[t] : rgba(225, 228, 235),
                            rgba(25, 30, 40), 3);
            c.drawTextCentered(btnTab[t].x, btnTab[t].y, btnTab[t].w, btnTab[t].h,
                               tabLabels[t], 3,
                               act ? rgba(255, 255, 255) : rgba(30, 35, 45), true);
        }
    }
};

int main(int argc, char **argv) {
    signal(SIGINT, handleSig);
    signal(SIGTERM, handleSig);

    HardwareTelemetry hw;
    hw.update();
    BenchmarkSuite bench;

    // Support CLI --self-test so we can verify exact benchmark performance from SSH
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) {
        printf("=== Ferrari Lab Hardware Self-Test on reMarkable Paper Pro ===\n");
        printf("SoC Temp: %.1f C | Battery: %.2f%% | Marker Battery: %d%%\n",
               hw.cpuTempC, hw.battCapacityPct, hw.markerBatteryPct);
        bench.runBenchmark1();
        printf("[1] %s -> %s\n", bench.metrics[0].title, bench.metrics[0].primaryText);
        bench.runBenchmark2();
        printf("[2] %s -> %s (%s)\n", bench.metrics[1].title, bench.metrics[1].primaryText, bench.metrics[1].secondaryText);
        bench.runBenchmark3();
        printf("[3] %s -> %s\n", bench.metrics[2].title, bench.metrics[2].primaryText);
        bench.runBenchmark4();
        printf("[4] %s -> %s\n", bench.metrics[3].title, bench.metrics[3].primaryText);
        printf("Composite Ferrari Score: %d\n", bench.compositeScore);
        return 0;
    }

    // Support CLI --dump-tabs <prefix> for offline/SSH pixel-accurate visual verification of all 3 tabs
    if (argc > 2 && strcmp(argv[1], "--dump-tabs") == 0) {
        const char *prefix = argv[2];
        std::vector<uint32_t> offscreen(RMPP_WIDTH * RMPP_HEIGHT, rgba(240, 242, 238));
        Canvas2D canvas(offscreen.data(), RMPP_WIDTH, RMPP_HEIGHT);
        ShowcaseStudio studio;
        LowLatencyInkLab inkLab;
        RawMarkerState marker;
        qtfb::Connection dummyConn;
        AppUI ui;

        bench.runBenchmark1();
        bench.runBenchmark2();
        bench.runBenchmark3();
        bench.runBenchmark4();
        bench.renderGamutAndSiemensChart();
        for (int i = 0; i < 40; i++) bench.pushTelemetry(hw);

        int prevX = 926, prevY = 310;
        for (int tab = 0; tab < 3; tab++) {
            ui.activeTab = tab;
            canvas.clear(rgba(240, 242, 238));
            ui.drawHeaderAndTabs(canvas, hw, (tab == 1 && studio.activeDemo < 3) ? REFRESH_MODE_ANIMATE : REFRESH_MODE_UFAST);
            if (tab == 0) {
                bench.drawPage(canvas, hw, prevX, prevY);
            } else if (tab == 1) {
                studio.drawPage(canvas, marker);
            } else {
                // Draw sample angle-responsive calligraphy & highlighter strokes on Tab 3 for visual check
                inkLab.drawPage(canvas, marker);
                inkLab.activeTool = LowLatencyInkLab::TOOL_CALLIGRAPHY;
                inkLab.activeColor = 0;
                marker.tiltX = 2400;
                marker.tiltY = -1800;
                inkLab.onPenDown(canvas, dummyConn, 200.0f, 490.0f, 0.55f, marker);
                for (int s = 1; s <= 70; s++) {
                    float t = (float)s / 70.0f;
                    float sx = 200.0f + t * 1160.0f;
                    float sy = 490.0f + sinf(t * 12.56f) * 58.0f;
                    float pr = 0.30f + 0.65f * (0.5f + 0.5f * cosf(t * 18.8f));
                    marker.tiltX = (int)(2600.0f * cosf(t * 6.28f));
                    marker.tiltY = (int)(2200.0f * sinf(t * 6.28f));
                    inkLab.onPenMove(canvas, dummyConn, sx, sy, pr, marker);
                }
                inkLab.onPenUp(canvas, dummyConn, marker);

                // Angle-responsive Yellow Highlighter pass rotating with Marker tilt
                inkLab.activeTool = LowLatencyInkLab::TOOL_HIGHLIGHTER;
                inkLab.activeColor = 4;
                marker.tiltX = 3100;
                marker.tiltY = -1200;
                inkLab.onPenDown(canvas, dummyConn, 220.0f, 650.0f, 0.68f, marker);
                for (int s = 1; s <= 60; s++) {
                    float t = (float)s / 60.0f;
                    float sx = 220.0f + t * 1120.0f;
                    float sy = 650.0f + cosf(t * 9.42f) * 42.0f;
                    marker.tiltX = (int)(3200.0f * cosf(t * 4.71f));
                    marker.tiltY = (int)(2800.0f * sinf(t * 4.71f));
                    inkLab.onPenMove(canvas, dummyConn, sx, sy, 0.72f, marker);
                }
                // Ultra-fine S Pencil stroke to verify razor-sharp smallest pencil tip
                inkLab.activeTool = LowLatencyInkLab::TOOL_TILT_PENCIL;
                inkLab.activeSize = 0; // S (Ultra-fine)
                inkLab.activeColor = 0;
                marker.tiltX = 1800;
                marker.tiltY = -1200;
                inkLab.onPenDown(canvas, dummyConn, 220.0f, 790.0f, 0.55f, marker);
                for (int s = 1; s <= 70; s++) {
                    float t = (float)s / 70.0f;
                    float sx = 220.0f + t * 1120.0f;
                    float sy = 790.0f + sinf(t * 18.84f) * 28.0f;
                    inkLab.onPenMove(canvas, dummyConn, sx, sy, 0.58f, marker);
                }
                inkLab.onPenUp(canvas, dummyConn, marker);

                // Keep live pressure & tilt populated in HUD for visual inspection
                inkLab.livePressure = 0.72f;
                inkLab.liveRawPressure = (int)(0.72f * 4095.0f);
                inkLab.updateTiltMetrics(2850, -1650, -0.52f);
                inkLab.activeTool = LowLatencyInkLab::TOOL_TILT_PENCIL;
                inkLab.activeSize = 0;
                inkLab.activeColor = 0;
                inkLab.drawPage(canvas, marker);
            }
            char path[256];
            snprintf(path, sizeof(path), "%s_tab%d.raw", prefix, tab);
            FILE *f = fopen(path, "wb");
            if (f) {
                fwrite(offscreen.data(), sizeof(uint32_t), offscreen.size(), f);
                fclose(f);
                printf("Saved %s\n", path);
            }
        }
        return 0;
    }

    qtfb::Connection conn;
    if (!conn.connectToAppload()) {
        fprintf(stderr, "Ferrari Lab: Failed to connect to AppLoad qtfb socket.\n");
        return 1;
    }

    RawMarkerState marker;
    marker.init();

    Canvas2D canvas(conn.pixels, conn.width, conn.height);
    ShowcaseStudio studio;
    LowLatencyInkLab inkLab;
    AppUI ui;

    conn.setRefreshMode(REFRESH_MODE_UFAST);

    int prevScreenX = 926, prevScreenY = 310;
    auto redrawFullPage = [&]() {
        if (ui.activeTab == 0 && bench.fullscreenPattern != 0) {
            bench.drawFullscreenPattern(canvas);
            conn.updateFull();
            return;
        }
        canvas.clear(rgba(240, 242, 238));
        ui.drawHeaderAndTabs(canvas, hw, conn.currentRefreshMode);
        if (ui.activeTab == 0) {
            bench.drawPage(canvas, hw, prevScreenX, prevScreenY);
        } else if (ui.activeTab == 1) {
            studio.drawPage(canvas, marker);
        } else {
            inkLab.drawPage(canvas, marker);
        }
        conn.updateFull();
    };

    // Run quick initial single-core & NEON benchmarks on startup so cards already show real numbers,
    // and keep the colorful Gamut & Siemens Star chart visible initially
    bench.runBenchmark1();
    bench.runBenchmark3();
    bench.renderGamutAndSiemensChart();
    hw.update();
    bench.pushTelemetry(hw);
    redrawFullPage();

    double lastTeleTime = nowSec();
    double lastAnimTime = nowSec();
    bool rawPenActiveInInk = false;

    while (gRunning) {
        // Wait for either raw Elan Marker Plus SPI input (/dev/input/event2) or AppLoad socket input
        struct pollfd pfds[2];
        int nfds = 0;
        if (conn.sockFD >= 0) {
            pfds[nfds].fd = conn.sockFD;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        if (marker.fd >= 0) {
            pfds[nfds].fd = marker.fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }
        int timeoutMs = (ui.activeTab == 2) ? (rawPenActiveInInk ? 1 : 4) : 10;
        if (nfds > 0) {
            ::poll(pfds, nfds, timeoutMs);
        } else {
            usleep(timeoutMs * 1000);
        }

        // 1. Drain high-rate 240Hz sub-pixel samples directly from /dev/input/event2
        // Coalesce all samples in a poll batch into a single tight dirty-rect flush for minimum latency
        marker.poll();
        if (ui.activeTab == 0 && bench.fullscreenPattern != 0 && marker.sampleCount > 0) {
            for (int i = 0; i < marker.sampleCount; i++) {
                const MarkerSample &s = marker.sampleQueue[i];
                if (s.touching) {
                    if (marker.toolRubber) {
                        bench.fullscreenPattern = 0;
                        bench.inspectorVisible = false;
                        redrawFullPage();
                        conn.requestFullRefresh(); // De-ghost after leaving the full-screen pattern
                        break;
                    } else {
                        bench.inspectFullscreenPixel(canvas, conn, (int)(s.sx + 0.5f), (int)(s.sy + 0.5f));
                    }
                }
            }
        } else if (ui.activeTab == 2 && marker.sampleCount > 0) {
            inkLab.resetDirtyRect();
            for (int i = 0; i < marker.sampleCount; i++) {
                const MarkerSample &s = marker.sampleQueue[i];
                marker.tiltX = s.tiltX;
                marker.tiltY = s.tiltY;
                if (s.touching) {
                    // Only draw directly via raw sub-pixel path when inside the paper canvas (sy >= PY)
                    if (s.sy >= (float)LowLatencyInkLab::PY &&
                        s.sx >= (float)LowLatencyInkLab::PX &&
                        s.sx <  (float)(LowLatencyInkLab::PX + LowLatencyInkLab::PW)) {
                        if (!rawPenActiveInInk) {
                            rawPenActiveInInk = true;
                            inkLab.onPenDown(canvas, conn, s.sx, s.sy, s.pressure, marker, true);
                        } else {
                            inkLab.onPenMove(canvas, conn, s.sx, s.sy, s.pressure, marker, true);
                        }
                    }
                } else {
                    if (rawPenActiveInInk) {
                        rawPenActiveInInk = false;
                        inkLab.onPenUp(canvas, conn, marker, true);
                    }
                }
            }
            inkLab.flushInkDirty(canvas, conn);
        }

        // 2. Drain qtfb socket events (finger touch, UI button taps, and fallback pen input)
        qtfb::ServerMessage msg;
        while (conn.pollPacket(msg)) {
            if (msg.type == MESSAGE_USERINPUT) {
                int it = msg.userInput.inputType;
                int x = msg.userInput.x;
                int y = msg.userInput.y;
                int d = msg.userInput.d;

                bool isPress = (it == INPUT_TOUCH_PRESS || it == INPUT_PEN_PRESS);
                bool isMove  = (it == INPUT_TOUCH_UPDATE || it == INPUT_PEN_UPDATE);
                bool isRel   = (it == INPUT_TOUCH_RELEASE || it == INPUT_PEN_RELEASE);
                bool isPenEv = (it == INPUT_PEN_PRESS || it == INPUT_PEN_UPDATE || it == INPUT_PEN_RELEASE);

                if (marker.rawPressure > 0) {
                    studio.normPressure = std::clamp((float)marker.rawPressure / 4095.0f, 0.05f, 1.0f);
                } else if (d > 0) {
                    studio.normPressure = std::clamp((float)d / 100.0f, 0.1f, 1.0f);
                } else {
                    studio.normPressure = 0.6f;
                }

                // Full-screen gallery pattern: Eraser exits (+ de-ghost), Pen tip inspects RGB/Gallery3 pixel
                if (ui.activeTab == 0 && bench.fullscreenPattern != 0) {
                    if (isPenEv && marker.toolRubber && isPress) {
                        bench.fullscreenPattern = 0;
                        bench.inspectorVisible = false;
                        redrawFullPage();
                        conn.requestFullRefresh(); // De-ghost after leaving the full-screen pattern
                    } else if (isPenEv && !marker.toolRubber && (isPress || isMove) && marker.fd < 0) {
                        bench.inspectFullscreenPixel(canvas, conn, x, y);
                    }
                    continue;
                }

                if (isPress) {
                    // Check top-level header buttons
                    if (ui.btnExit.contains(x, y)) {
                        gRunning = false;
                        break;
                    }
                    if (ui.btnDeghost.contains(x, y)) {
                        conn.requestFullRefresh();
                        continue;
                    }
                    if (ui.btnWaveform.contains(x, y)) {
                        int nextMode = (conn.currentRefreshMode + 1) % 5;
                        conn.setRefreshMode(nextMode);
                        ui.drawHeaderAndTabs(canvas, hw, conn.currentRefreshMode);
                        conn.updateRegion(0, 0, canvas.W, 100);
                        continue;
                    }
                    // Check 3-Tab selector bar
                    bool switchedTab = false;
                    for (int t = 0; t < 3; t++) {
                        if (ui.btnTab[t].contains(x, y) && ui.activeTab != t) {
                            if (rawPenActiveInInk) {
                                rawPenActiveInInk = false;
                                inkLab.onPenUp(canvas, conn, marker);
                            }
                            ui.activeTab = t;
                            if (t == 1) {
                                conn.setRefreshMode(studio.activeDemo < 3 ? REFRESH_MODE_ANIMATE : REFRESH_MODE_UFAST);
                            } else if (t == 2) {
                                // Native xochitl-style ink: instant mono trace + two-sweep color settle (CONTENT)
                                conn.setRefreshMode(REFRESH_MODE_CONTENT);
                                inkLab.curRefreshMode = REFRESH_MODE_CONTENT;
                            } else {
                                conn.setRefreshMode(REFRESH_MODE_UFAST);
                            }
                            redrawFullPage();
                            switchedTab = true;
                            break;
                        }
                    }
                    if (switchedTab) continue;

                    if (ui.activeTab == 0) {
                        if (bench.btnRunAll.contains(x, y)) {
                            bench.runningAll = true;
                            redrawFullPage();
                            bench.runBenchmark1(); hw.update(); bench.pushTelemetry(hw); redrawFullPage();
                            bench.runBenchmark2(); hw.update(); bench.pushTelemetry(hw); redrawFullPage();
                            bench.runBenchmark3(); hw.update(); bench.pushTelemetry(hw); redrawFullPage();
                            bench.runBenchmark4(); hw.update(); bench.pushTelemetry(hw); redrawFullPage();
                            bench.runBenchmark5(conn, prevScreenX, prevScreenY);
                            bench.runningAll = false;
                            hw.update(); bench.pushTelemetry(hw);
                            redrawFullPage();
                            continue;
                        }
                        bool ranOne = false;
                        for (int i = 0; i < 5; i++) {
                            if (bench.btnRunSingle[i].contains(x, y)) {
                                if (i == 0) bench.runBenchmark1();
                                else if (i == 1) bench.runBenchmark2();
                                else if (i == 2) bench.runBenchmark3();
                                else if (i == 3) bench.runBenchmark4();
                                else if (i == 4) bench.runBenchmark5(conn, prevScreenX, prevScreenY);
                                hw.update();
                                bench.pushTelemetry(hw);
                                redrawFullPage();
                                ranOne = true;
                                break;
                            }
                        }
                        if (ranOne) continue;

                        for (int v = 0; v < 3; v++) {
                            if (bench.btnVisMode[v].contains(x, y)) {
                                if (v == 0) bench.renderGamutAndSiemensChart();
                                else if (v == 1) bench.runBenchmark2();
                                else if (v == 2) bench.runBenchmark4();
                                redrawFullPage();
                                break;
                            }
                        }
                        // Tap a gallery pattern -> show it full screen
                        if (bench.handlePreviewTap(x, y)) {
                            redrawFullPage();
                            conn.requestFullRefresh(); // De-ghost after showing the full-screen pattern
                            continue;
                        }
                    } else if (ui.activeTab == 1) {
                        // Tab 2: Interactive Showcase Studio
                        int prevDemo = studio.activeDemo;
                        if (studio.handleTap(x, y)) {
                            if (studio.activeDemo != prevDemo) {
                                conn.setRefreshMode(studio.activeDemo < 3 ? REFRESH_MODE_ANIMATE : REFRESH_MODE_UFAST);
                            }
                            redrawFullPage();
                            continue;
                        }
                        if (x >= ShowcaseStudio::VX && x < ShowcaseStudio::VX + ShowcaseStudio::VW &&
                            y >= ShowcaseStudio::VY + 90 && y < ShowcaseStudio::VY + ShowcaseStudio::VH) {
                            studio.pointerDown = true;
                            studio.ptrX = x;
                            studio.ptrY = y;
                            studio.prevPtrX = x;
                            studio.prevPtrY = y;
                            if (studio.activeDemo == 0) {
                                studio.applyTouchWaveAt(x, y, 0.85f);
                            } else if (studio.activeDemo == 3) {
                                studio.paintStrokeSegment(x, y, x, y, studio.normPressure, marker);
                                studio.drawCalligrapherDemo(canvas, marker);
                                conn.updateRegion(std::max(ShowcaseStudio::VX, x - 90),
                                                  std::max(ShowcaseStudio::VY + 90, y - 90), 180, 180);
                            }
                        }
                    } else if (ui.activeTab == 2) {
                        // Tab 3: Low-Latency Ink Lab
                        // Toolbar buttons (y < PY) accept both finger & Marker taps
                        if (inkLab.handleTap(canvas, conn, x, y, marker)) {
                            continue;
                        }
                        // Drawing area (y >= PY) is strictly Marker-only (ignore finger touches!)
                        if (isPenEv && marker.fd < 0 &&
                            y >= LowLatencyInkLab::PY &&
                            x >= LowLatencyInkLab::PX &&
                            x < LowLatencyInkLab::PX + LowLatencyInkLab::PW) {
                            inkLab.onPenDown(canvas, conn, (float)x, (float)y, studio.normPressure, marker);
                        }
                    }
                } else if (isMove) {
                    if (ui.activeTab == 1 && studio.pointerDown) {
                        if (studio.activeDemo == 0) {
                            studio.applyTouchWaveAt(x, y, 0.55f);
                        } else if (studio.activeDemo == 1) {
                            int dx = x - studio.ptrX;
                            int dy = y - studio.ptrY;
                            studio.rotY += (float)dx * 0.008f;
                            studio.rotX += (float)dy * 0.008f;
                        } else if (studio.activeDemo == 3) {
                            studio.paintStrokeSegment(studio.ptrX, studio.ptrY, x, y, studio.normPressure, marker);
                            studio.drawCalligrapherDemo(canvas, marker);
                            int minX = std::max(ShowcaseStudio::VX, std::min(studio.ptrX, x) - 95);
                            int minY = std::max(ShowcaseStudio::VY + 90, std::min(studio.ptrY, y) - 95);
                            int maxX = std::min(ShowcaseStudio::VX + ShowcaseStudio::VW, std::max(studio.ptrX, x) + 95);
                            int maxY = std::min(ShowcaseStudio::VY + ShowcaseStudio::VH, std::max(studio.ptrY, y) + 95);
                            conn.updateRegion(minX, minY, maxX - minX, maxY - minY);
                        }
                        studio.prevPtrX = studio.ptrX;
                        studio.prevPtrY = studio.ptrY;
                        studio.ptrX = x;
                        studio.ptrY = y;
                    } else if (ui.activeTab == 2 && isPenEv && marker.fd < 0 && inkLab.penDown) {
                        inkLab.onPenMove(canvas, conn, (float)x, (float)y, studio.normPressure, marker);
                    }
                } else if (isRel) {
                    studio.pointerDown = false;
                    if (ui.activeTab == 2 && isPenEv && marker.fd < 0 && inkLab.penDown) {
                        inkLab.onPenUp(canvas, conn, marker);
                    }
                }
            }
        }

        if (ui.activeTab == 2) {
            inkLab.pollColorSettle(canvas, conn);
            inkLab.pollHudUpdate(canvas, conn, marker);
        }

        double now = nowSec();
        // Periodic live simulation step in Tab 2 for Demos 0, 1, 2
        if (ui.activeTab == 1 && studio.activeDemo < 3 && (now - lastAnimTime) >= 0.085) {
            lastAnimTime = now;
            if (studio.activeDemo == 0) {
                studio.stepWavePhysics();
                studio.drawWaveFabricDemo(canvas);
            } else if (studio.activeDemo == 1) {
                studio.drawPolyhedraDemo(canvas, marker);
            } else if (studio.activeDemo == 2) {
                studio.stepParticles(marker);
                studio.drawParticleVortexDemo(canvas);
            }
            conn.updateRegion(ShowcaseStudio::VX, ShowcaseStudio::VY + 90, ShowcaseStudio::VW, ShowcaseStudio::VH - 90);
        }

        // Periodic hardware sensor telemetry update (paused during & for 3s after writing in Tab 3)
        bool inkBusy = (ui.activeTab == 2) &&
                       (inkLab.penDown || rawPenActiveInInk || inkLab.hasPendingSwtconWork() ||
                        !inkLab.settleQueue.empty() || inkLab.hasActiveSettleSeg ||
                        (inkLab.inkNowSec() - inkLab.lastInkFlushSec) < 3.0);
        bool fullscreenPatternShown = (ui.activeTab == 0 && bench.fullscreenPattern != 0);
        if (!inkBusy && !fullscreenPatternShown && (now - lastTeleTime) >= 1.8) {
            lastTeleTime = now;
            hw.update();
            bench.pushTelemetry(hw);
            ui.drawHeaderAndTabs(canvas, hw, conn.currentRefreshMode);
            conn.updateRegion(0, 0, 900, 92);
            if (ui.activeTab == 2) {
                inkLab.lastSwtconDispatchSec = inkLab.inkNowSec();
            }
            if (ui.activeTab == 0) {
                bench.drawPage(canvas, hw, prevScreenX, prevScreenY);
                conn.updateRegion(910, 1184, 686, 956);
                conn.updateRegion(24, 1612, 868, 528);
            }
        }
    }

    return 0;
}
