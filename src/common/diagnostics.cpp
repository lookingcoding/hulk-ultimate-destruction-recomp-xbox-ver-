#include <chrono>
#include <cstdlib>
#include <thread>
#include "common/diagnostics.h"
#include "common/replay.h"
#include "gpu/backend.h"

void hulk::InitDiagnostics() {
    InitReplay();
    // Export a frame through the renderer's own diagnostic capture, without
    // capturing other desktop applications. Disabled in ordinary runs.
    const char* delayText = std::getenv("HULK_CAPTURE_DELAY_MS");
    if (!delayText) return;
    long delay = std::strtol(delayText, nullptr, 10);
    if (delay < 100 || delay > 60000) return;
    const char* intervalText = std::getenv("HULK_CAPTURE_INTERVAL_MS");
    const char* countText = std::getenv("HULK_CAPTURE_COUNT");
    long interval = intervalText ? std::strtol(intervalText, nullptr, 10) : 10000;
    long count = countText ? std::strtol(countText, nullptr, 10) : 1;
    if (interval < 1000 || interval > 60000 || count < 1 || count > 20) return;
    std::thread([delay, interval, count] {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        for (long i = 0; i < count; ++i) {
            gpu::g_capture.store(true);
            if (i + 1 < count) std::this_thread::sleep_for(std::chrono::milliseconds(interval));
        }
    }).detach();
}
