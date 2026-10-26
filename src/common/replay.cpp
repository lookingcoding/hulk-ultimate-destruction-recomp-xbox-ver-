// Optional application test input. This feeds the game's controller API;
// it never sends OS keyboard/mouse input or reads other applications.
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include "common/replay.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace {
using Clock = std::chrono::steady_clock;
struct Event { long at, duration; std::string buttons; bool announced = false; };
std::vector<Event> events;
Clock::time_point started;
Clock::time_point nextCheck;
std::mutex replayMutex;
std::filesystem::path replayPath;
std::filesystem::file_time_type modified;
}

static void LoadReplay(long elapsed) {
    std::ifstream file(replayPath);
    if (!file) { LOGE("test input: cannot open replay"); events.clear(); return; }
    std::vector<Event> loaded;
    std::string line;
    long previous = -1;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        Event event{};
        std::istringstream input(line);
        if (!(input >> event.at >> event.duration >> event.buttons) || event.at < previous ||
            event.at < 0 || event.at > 86400000 || event.duration < 1 || event.duration > 60000) {
            LOGE("test input: invalid event; replay disabled");
            events.clear();
            return;
        }
        std::istringstream names(event.buttons);
        std::string name;
        while (std::getline(names, name, '+')) {
            if (name != "START" && name != "BACK" && name != "A" && name != "B" && name != "X" && name != "Y" &&
                name != "UP" && name != "DOWN" && name != "LEFT" && name != "RIGHT" &&
                name != "LUP" && name != "LDOWN" && name != "LLEFT" && name != "LRIGHT" &&
                name != "RUP" && name != "RDOWN" && name != "RLEFT" && name != "RRIGHT" &&
                name != "LT" && name != "RT" && name != "WHITE" && name != "BLACK" &&
                name != "LS" && name != "RS" && name != "CAPTURE") {
                LOGE("test input: unknown button %s; replay disabled", name.c_str());
                events.clear();
                return;
            }
        }
        previous = event.at;
        event.announced = event.at < elapsed;
        loaded.push_back(event);
    }
    events = std::move(loaded);
    LOGI("test input: loaded %zu events, elapsed %ld ms", events.size(), elapsed);
}

void hulk::InitReplay() {
    std::lock_guard lock(replayMutex);
    started = Clock::now();
    nextCheck = started;
    const char* path = std::getenv("HULK_TEST_INPUT");
    if (!path) return;
    replayPath = path;
    std::error_code ec;
    modified = std::filesystem::last_write_time(replayPath, ec);
    LoadReplay(0);
}

void hulk::ReplayPad(int port, input::Pad& pad) {
    if (port || replayPath.empty()) return;
    std::lock_guard lock(replayMutex);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started).count();
    if (Clock::now() >= nextCheck) {
        nextCheck = Clock::now() + std::chrono::milliseconds(250);
        std::error_code ec;
        auto stamp = std::filesystem::last_write_time(replayPath, ec);
        if (!ec && stamp != modified) { modified = stamp; LoadReplay(long(elapsed)); }
    }
    for (auto& event : events) {
        if (elapsed < event.at || elapsed >= event.at + event.duration) continue;
        const bool first = !event.announced;
        if (first) LOGI("test input: %ld ms %s", event.at, event.buttons.c_str());
        event.announced = true;
        std::istringstream names(event.buttons);
        std::string name;
        while (std::getline(names, name, '+')) {
            if (name == "START") pad.buttons |= 0x10;
            else if (name == "BACK") pad.buttons |= 0x20;
            else if (name == "UP") pad.buttons |= 1;
            else if (name == "DOWN") pad.buttons |= 2;
            else if (name == "LEFT") pad.buttons |= 4;
            else if (name == "RIGHT") pad.buttons |= 8;
            else if (name == "A") pad.analog[0] = 255;
            else if (name == "B") pad.analog[1] = 255;
            else if (name == "X") pad.analog[2] = 255;
            else if (name == "Y") pad.analog[3] = 255;
            else if (name == "LUP") pad.ly = 32767;
            else if (name == "LDOWN") pad.ly = -32767;
            else if (name == "LLEFT") pad.lx = -32767;
            else if (name == "LRIGHT") pad.lx = 32767;
            else if (name == "RUP") pad.ry = 32767;
            else if (name == "RDOWN") pad.ry = -32767;
            else if (name == "RLEFT") pad.rx = -32767;
            else if (name == "RRIGHT") pad.rx = 32767;
            else if (name == "LT") pad.analog[6] = 255;
            else if (name == "RT") pad.analog[7] = 255;
            else if (name == "WHITE") pad.analog[5] = 255;
            else if (name == "BLACK") pad.analog[4] = 255;
            else if (name == "LS") pad.buttons |= 0x40;
            else if (name == "RS") pad.buttons |= 0x80;
            else if (name == "CAPTURE" && first) gpu::g_capture.store(true);
        }
        pad.connected = true;
    }
}
