#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "app/app.h"
#include "common/replay.h"
#include "core/input.h"
#include "core/log.h"
#include "gpu/backend.h"

namespace app { unsigned window_reads = 0; void* Window() { ++window_reads; return nullptr; } }
namespace logx { void Write(Level, const char*, ...) {} }

int main(int argc, char** argv) {
    if (argc != 2) return 1;
    const auto dir = std::filesystem::path(argv[1]) /
        ("internal-pad-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(dir);
    const auto replay = dir / "replay.input";
    std::ofstream(replay) << "0 60000 A+X+LUP+RT+CAPTURE\n";
    _putenv_s("HULK_TEST_INPUT", replay.string().c_str());
    _putenv_s("HULK_TEST_INTERNAL_PAD", "1");
    _wputenv_s(L"HULK_TEST_EXE", L"D:\\not-this-process.exe");
    hulk::InitReplay();
    input::Pad pad{};
    hulk::ReplayPad(0, pad);
    if (hulk::InternalPadTestMode() || pad.connected || pad.analog[0] || gpu::g_capture) return 2;

    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, DWORD(std::size(executable)));
    _wputenv_s(L"HULK_TEST_EXE", executable);
    hulk::InitReplay();
    if (!hulk::InternalPadTestMode()) return 3;
    app::window_reads = 0;
    input::Pad untouched{}; untouched.analog[1] = 42;
    hulk::ReplayPad(0, pad);
    if (!pad.connected || pad.analog[0] != 255 || pad.analog[2] != 255 || pad.analog[7] != 255 ||
        pad.ly != 32767 || !gpu::g_capture || untouched.analog[1] != 42 || app::window_reads) return 4;
    input::Pad other_port{};
    hulk::ReplayPad(1, other_port);
    if (other_port.connected || other_port.analog[0]) return 5;

    _putenv_s("HULK_TEST_INTERNAL_PAD", "");
    hulk::InitReplay();
    pad = {};
    hulk::ReplayPad(0, pad);
    if (hulk::InternalPadTestMode() || pad.connected || pad.analog[0]) return 6;
    std::puts("internal Pad fixture: exact-executable opt-in and default foreground block passed; no window created");
    return 0;
}
