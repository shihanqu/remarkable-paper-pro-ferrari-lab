#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <time.h>
#include <dirent.h>
#include <algorithm>

#define SOCKET_PATH "/tmp/qtfb.sock"
#define RMPP_WIDTH 1620
#define RMPP_HEIGHT 2160

#define MESSAGE_INITIALIZE 0
#define MESSAGE_UPDATE 1
#define MESSAGE_CUSTOM_INITIALIZE 2
#define MESSAGE_TERMINATE 3
#define MESSAGE_USERINPUT 4
#define MESSAGE_SET_REFRESH_MODE 5
#define MESSAGE_REQUEST_FULL_REFRESH 6
#define MESSAGE_DEVICE_STATE_CHANGED 7
#define MESSAGE_DEVICE_STATE_INIT 8

#define FBFMT_RM2FB 0
#define FBFMT_RMPP_RGB888 1
#define FBFMT_RMPP_RGBA8888 2
#define FBFMT_RMPP_RGB565 3

#define UPDATE_ALL 0
#define UPDATE_PARTIAL 1

#define REFRESH_MODE_UFAST 0
#define REFRESH_MODE_FAST 1
#define REFRESH_MODE_ANIMATE 2
#define REFRESH_MODE_CONTENT 3
#define REFRESH_MODE_UI 4
#define REFRESH_MODE_SLEEP 5

#define INPUT_TOUCH_PRESS 0x10
#define INPUT_TOUCH_RELEASE 0x11
#define INPUT_TOUCH_UPDATE 0x12

#define INPUT_PEN_PRESS 0x20
#define INPUT_PEN_RELEASE 0x21
#define INPUT_PEN_UPDATE 0x22

#define INPUT_BTN_PRESS 0x30
#define INPUT_BTN_RELEASE 0x31

namespace qtfb {
    typedef int FBKey;

    struct InitMessageContents {
        FBKey framebufferKey;
        uint8_t framebufferType;
    };

    struct CustomInitMessageContents {
        FBKey framebufferKey;
        uint8_t framebufferType;
        uint16_t width;
        uint16_t height;
    };

    struct InitMessageResponseContents {
        int shmKeyDefined;
        size_t shmSize;
    };

    struct UpdateRegionMessageContents {
        int type;
        int x, y, w, h;
    };

    struct UserInputContents {
        int inputType;
        int devId;
        int x, y, d;
    };

    struct DeviceStateChangedContents {
        int reason;
        union {
            struct {
                int rotation;
            } rotation;
        };
    };

    struct ClientMessage {
        uint8_t type;
        union {
            struct InitMessageContents init;
            struct UpdateRegionMessageContents update;
            struct CustomInitMessageContents customInit;
            int refreshMode;
        };
    };

    struct ServerMessage {
        uint8_t type;
        union {
            struct InitMessageResponseContents init;
            struct UserInputContents userInput;
            struct DeviceStateChangedContents deviceStateChanged;
        };
    };

    class Connection {
    public:
        int sockFD = -1;
        int shmFD = -1;
        uint32_t *pixels = nullptr;
        size_t shmSize = 0;
        int width = RMPP_WIDTH;
        int height = RMPP_HEIGHT;
        int currentRefreshMode = REFRESH_MODE_UFAST;

        bool connectToAppload() {
            const char *keyEnv = getenv("QTFB_KEY");
            FBKey fbKey = keyEnv ? (FBKey)strtoul(keyEnv, nullptr, 10) : 245209899;

            sockFD = socket(AF_UNIX, SOCK_SEQPACKET, 0);
            if (sockFD < 0) return false;

            struct sockaddr_un addr;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

            if (connect(sockFD, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
                close(sockFD);
                sockFD = -1;
                return false;
            }

            ClientMessage initMsg;
            memset(&initMsg, 0, sizeof(initMsg));
            initMsg.type = MESSAGE_INITIALIZE;
            initMsg.init.framebufferKey = fbKey;
            initMsg.init.framebufferType = FBFMT_RMPP_RGBA8888;

            if (send(sockFD, &initMsg, sizeof(initMsg), 0) < 0) {
                close(sockFD);
                sockFD = -1;
                return false;
            }

            ServerMessage resp;
            memset(&resp, 0, sizeof(resp));
            if (recv(sockFD, &resp, sizeof(resp), 0) < 1) {
                close(sockFD);
                sockFD = -1;
                return false;
            }

            int flags = fcntl(sockFD, F_GETFL, 0);
            if (flags != -1) {
                fcntl(sockFD, F_SETFL, flags | O_NONBLOCK);
            }

            char shmPath[64];
            snprintf(shmPath, sizeof(shmPath), "/dev/shm/qtfb_%d", resp.init.shmKeyDefined);
            shmFD = open(shmPath, O_RDWR);
            if (shmFD < 0) {
                snprintf(shmPath, sizeof(shmPath), "/qtfb_%d", resp.init.shmKeyDefined);
                shmFD = shm_open(shmPath, O_RDWR, 0);
            }
            if (shmFD < 0) {
                close(sockFD);
                sockFD = -1;
                return false;
            }

            shmSize = resp.init.shmSize;
            void *map = mmap(nullptr, shmSize, PROT_READ | PROT_WRITE, MAP_SHARED, shmFD, 0);
            if (map == MAP_FAILED) {
                close(shmFD);
                close(sockFD);
                shmFD = -1;
                sockFD = -1;
                return false;
            }
            pixels = (uint32_t *)map;
            return true;
        }

        ~Connection() {
            if (pixels && shmSize) {
                munmap(pixels, shmSize);
            }
            if (shmFD >= 0) close(shmFD);
            if (sockFD >= 0) {
                ClientMessage msg;
                memset(&msg, 0, sizeof(msg));
                msg.type = MESSAGE_TERMINATE;
                send(sockFD, &msg, sizeof(msg), 0);
                close(sockFD);
            }
        }

        void updateFull() {
            if (sockFD < 0) return;
            ClientMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MESSAGE_UPDATE;
            msg.update.type = UPDATE_ALL;
            send(sockFD, &msg, sizeof(msg), 0);
        }

        void updateRegion(int x, int y, int w, int h) {
            if (sockFD < 0) return;
            if (x < 0) { w += x; x = 0; }
            if (y < 0) { h += y; y = 0; }
            if (x + w > width) w = width - x;
            if (y + h > height) h = height - y;
            if (w <= 0 || h <= 0) return;
            ClientMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MESSAGE_UPDATE;
            msg.update.type = UPDATE_PARTIAL;
            msg.update.x = x;
            msg.update.y = y;
            msg.update.w = w;
            msg.update.h = h;
            send(sockFD, &msg, sizeof(msg), 0);
        }

        void setRefreshMode(int mode) {
            if (sockFD < 0) return;
            currentRefreshMode = mode;
            ClientMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MESSAGE_SET_REFRESH_MODE;
            msg.refreshMode = mode;
            send(sockFD, &msg, sizeof(msg), 0);
        }

        void requestFullRefresh() {
            if (sockFD < 0) return;
            ClientMessage msg;
            memset(&msg, 0, sizeof(msg));
            msg.type = MESSAGE_REQUEST_FULL_REFRESH;
            send(sockFD, &msg, sizeof(msg), 0);
        }

        bool pollPacket(ServerMessage &out) {
            if (sockFD < 0) return false;
            ssize_t r = recv(sockFD, &out, sizeof(out), 0);
            return r > 0;
        }
    };
}

// High-rate sub-pixel sample from Elan Marker Plus (/dev/input/event2)
struct MarkerSample {
    float sx;         // Screen X in 0..1620 (sub-pixel precision)
    float sy;         // Screen Y in 0..2160 (sub-pixel precision)
    float pressure;   // Normalized 0..1 (from 12-bit 0..4095)
    int tiltX;
    int tiltY;
    bool touching;
    bool toolRubber;
};

// Raw Elan Marker Plus hardware reader (/dev/input/event2)
// Reads live sub-pixel X (0..11180), Y (0..15340), 12-bit pressure (0..4095),
// hover distance, X/Y tilt, and physical eraser flip at full 240Hz SPI rate.
struct RawMarkerState {
    int fd = -1;
    int rawAbsX = 5590;    // 0..11180
    int rawAbsY = 7670;    // 0..15340
    int maxAbsX = 11180;
    int maxAbsY = 15340;
    int rawPressure = 0;   // 0..4095
    int rawDistance = 0;   // 0..65535
    int tiltX = 0;         // -9000..9000
    int tiltY = 0;
    bool btnTouch = false;
    bool toolRubber = false;
    bool toolPen = false;
    bool hovering = false;

    static constexpr int MAX_SAMPLES = 64;
    MarkerSample sampleQueue[MAX_SAMPLES];
    int sampleCount = 0;

    void init() {
        fd = open("/dev/input/event2", O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
#ifdef EVIOCGABS
            struct input_absinfo info;
            if (ioctl(fd, EVIOCGABS(ABS_X), &info) == 0 && info.maximum > 0) {
                rawAbsX = info.value;
                maxAbsX = info.maximum;
            }
            if (ioctl(fd, EVIOCGABS(ABS_Y), &info) == 0 && info.maximum > 0) {
                rawAbsY = info.value;
                maxAbsY = info.maximum;
            }
#endif
        }
    }

    void poll() {
        sampleCount = 0;
        if (fd < 0) return;
        struct input_event ev;
        bool dirtyFrame = false;
        while (read(fd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
            if (ev.type == EV_ABS) {
                switch (ev.code) {
                    case 0x00: /* ABS_X */ rawAbsX = ev.value; dirtyFrame = true; break;
                    case 0x01: /* ABS_Y */ rawAbsY = ev.value; dirtyFrame = true; break;
                    case ABS_PRESSURE: rawPressure = ev.value; dirtyFrame = true; break;
                    case ABS_DISTANCE: rawDistance = ev.value; hovering = (ev.value > 0 && rawPressure == 0); break;
                    case ABS_TILT_X:   tiltX = ev.value; break;
                    case ABS_TILT_Y:   tiltY = ev.value; break;
                    default: break;
                }
            } else if (ev.type == EV_KEY) {
                if (ev.code == BTN_TOOL_RUBBER) { toolRubber = (ev.value != 0); dirtyFrame = true; }
                if (ev.code == BTN_TOOL_PEN)    { toolPen = (ev.value != 0); dirtyFrame = true; }
                if (ev.code == 0x14a /* BTN_TOUCH */) { btnTouch = (ev.value != 0); dirtyFrame = true; }
            } else if (ev.type == 0x00 /* EV_SYN */ && ev.code == 0x00 /* SYN_REPORT */) {
                if (dirtyFrame && sampleCount < MAX_SAMPLES) {
                    float sx = (float)rawAbsX * ((float)RMPP_WIDTH / (float)maxAbsX);
                    float sy = (float)rawAbsY * ((float)RMPP_HEIGHT / (float)maxAbsY);
                    sx = std::clamp(sx, 0.0f, (float)(RMPP_WIDTH - 1));
                    sy = std::clamp(sy, 0.0f, (float)(RMPP_HEIGHT - 1));
                    bool isTouching = btnTouch || (rawPressure > 10);
                    float normP = std::clamp((float)rawPressure / 4095.0f, 0.0f, 1.0f);
                    sampleQueue[sampleCount++] = {
                        sx, sy, normP, tiltX, tiltY, isTouching, toolRubber
                    };
                    dirtyFrame = false;
                }
            }
        }
    }

    ~RawMarkerState() {
        if (fd >= 0) close(fd);
    }
};

// Live Hardware Telemetry Reader (/sys & /proc)
struct HardwareTelemetry {
    float cpuTempC = 40.0f;
    float battTempC = 26.0f;
    float battCapacityPct = 100.0f;
    float battCurrentMa = 0.0f;
    float battVoltageMv = 4100.0f;
    float battPowerMw = 0.0f;
    int markerBatteryPct = 95;
    int frontlight = 0;
    int frontlightMax = 2047;
    int cpuFreqMhz[4] = {1800, 1800, 1800, 1800};
    float cpuLoadPct[4] = {0, 0, 0, 0};
    float totalCpuLoadPct = 0.0f;
    int memUsedMb = 0;
    int memTotalMb = 2048;

    uint64_t prevTotal[5] = {0};
    uint64_t prevIdle[5] = {0};

    static long readLongFile(const char *path, long fallback = 0) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return fallback;
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return fallback;
        buf[n] = '\0';
        return strtol(buf, nullptr, 10);
    }

    static float readFloatFile(const char *path, float fallback = 0.0f) {
        int fd = open(path, O_RDONLY);
        if (fd < 0) return fallback;
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) return fallback;
        buf[n] = '\0';
        return strtof(buf, nullptr);
    }

    void update() {
        cpuTempC = readLongFile("/sys/class/thermal/thermal_zone1/temp", 40000) / 1000.0f;
        battTempC = readLongFile("/sys/class/thermal/thermal_zone0/temp", 26000) / 1000.0f;
        battCapacityPct = readFloatFile("/sys/class/power_supply/max1726x_battery/capacity_hires",
                          (float)readLongFile("/sys/class/power_supply/max1726x_battery/capacity", 100));
        long curUa = readLongFile("/sys/class/power_supply/max1726x_battery/current_now", 0);
        long voltUv = readLongFile("/sys/class/power_supply/max1726x_battery/voltage_now", 4000000);
        battCurrentMa = curUa / 1000.0f;
        battVoltageMv = voltUv / 1000.0f;
        battPowerMw = (battCurrentMa * battVoltageMv) / 1000.0f;

        markerBatteryPct = (int)readLongFile("/sys/class/power_supply/elants-marker-battery/capacity", 95);
        frontlight = (int)readLongFile("/sys/class/backlight/rm_frontlight/actual_brightness", 0);
        frontlightMax = (int)readLongFile("/sys/class/backlight/rm_frontlight/max_brightness", 2047);

        for (int i = 0; i < 4; i++) {
            char p[96];
            snprintf(p, sizeof(p), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", i);
            cpuFreqMhz[i] = (int)(readLongFile(p, 1800000) / 1000);
        }

        // Parse /proc/stat for CPU utilization
        FILE *f = fopen("/proc/stat", "r");
        if (f) {
            char line[256];
            int idx = 0;
            while (fgets(line, sizeof(line), f) && idx < 5) {
                if (strncmp(line, "cpu", 3) == 0) {
                    char label[16];
                    unsigned long long u = 0, n = 0, s = 0, id = 0, io = 0, irq = 0, sirq = 0, st = 0;
                    if (sscanf(line, "%15s %llu %llu %llu %llu %llu %llu %llu %llu",
                               label, &u, &n, &s, &id, &io, &irq, &sirq, &st) >= 5) {
                        uint64_t idle = id + io;
                        uint64_t total = u + n + s + idle + irq + sirq + st;
                        uint64_t dTotal = total - prevTotal[idx];
                        uint64_t dIdle = idle - prevIdle[idx];
                        prevTotal[idx] = total;
                        prevIdle[idx] = idle;
                        float pct = (dTotal > 0) ? (100.0f * (float)(dTotal - dIdle) / (float)dTotal) : 0.0f;
                        if (pct < 0.0f) pct = 0.0f;
                        if (pct > 100.0f) pct = 100.0f;
                        if (idx == 0) totalCpuLoadPct = pct;
                        else cpuLoadPct[idx - 1] = pct;
                        idx++;
                    }
                }
            }
            fclose(f);
        }

        // Parse /proc/meminfo
        FILE *mf = fopen("/proc/meminfo", "r");
        if (mf) {
            char line[128];
            long totalKb = 2048 * 1024, availKb = 1024 * 1024;
            while (fgets(line, sizeof(line), mf)) {
                if (sscanf(line, "MemTotal: %ld kB", &totalKb) == 1) continue;
                if (sscanf(line, "MemAvailable: %ld kB", &availKb) == 1) break;
            }
            fclose(mf);
            memTotalMb = (int)(totalKb / 1024);
            memUsedMb = (int)((totalKb - availKb) / 1024);
        }
    }
};
