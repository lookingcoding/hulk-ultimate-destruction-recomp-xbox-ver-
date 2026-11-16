#include "common/native_controls.h"
#include "common/bindings.h"
#include "common/addresses.h"
#include "kernel/kernel.h"
#include "app/app.h"
#include "core/log.h"
#include "core/input.h"
#include <bit>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {
#ifdef HULK_USA
constexpr uint32_t raw_text = 0xF79A0, slider = 0xF1A70, show_dialog = 0xBE230, close_dialog = 0xE63A0;
#else
constexpr uint32_t raw_text = 0xF7BC0, slider = 0xF1C90, show_dialog = 0xBE390, close_dialog = 0xE65C0;
#endif
int failures = 0, saved = 0;
float x = 1, y = 1;
bool inverted = false;
std::map<std::string, uint32_t> widgets;
std::map<uint32_t, std::u16string> captions;
std::vector<uint8_t> memory(0x600000);
void Check(bool ok, const char* label) { if (!ok) { std::printf("FAIL: %s\n", label); failures++; } }
void Close(uint32_t choice) { kernel::W32(0x20000 + 0x2218, 0); kernel::W32(0x20000 + 0x221c, choice); }
}
namespace app { void Save() { saved++; } }
namespace hulk { void NoteSettingChanged() {} bool BackgroundTest() { return false; } }
namespace logx { void Write(Level, const char*, ...) {} }
namespace kernel {
uint8_t* g_base = memory.data();
uint32_t CallGuest(X86Context& ctx, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == hulk::addr::kFindWidget) {
        std::string name(reinterpret_cast<char*>(HostPtr(args[0])));
        auto [it, inserted] = widgets.emplace(name, uint32_t(0x30000 + widgets.size() * 0x300));
        W32(it->second, 0x28000); W32(0x28064, 0x777000);
        return it->second;
    }
    if (fn == raw_text) {
        captions[ctx.ecx - 0x18] = reinterpret_cast<const char16_t*>(HostPtr(args[0])); return 0;
    }
    if (fn == slider || fn == 0x777000) return 0;
    if (fn == show_dialog) {
        Check(count == 8 && ctx.ecx == 0x20000, "native dialog ABI");
        Check(std::string(reinterpret_cast<char*>(HostPtr(args[0]))) == "pc_controls_dialog", "native dialog template");
        W32(0x22218, 0x24000); return 1;
    }
    if (fn == close_dialog) { Close(1); return 0; }
    std::printf("Unexpected native API %08x\n", fn); failures++; return 0;
}
}
int main() {
    using namespace hulk;
    using namespace hulk::bindings;
    SetControlsMouseHooks({[] { return x; }, [](float v) { x = v; }, [] { return y; }, [](float v) { y = v; },
                          [] { return inverted; }, [](bool v) { inverted = v; }});
    kernel::W32(addr::kFrontend, 0x20000);
    X86Context ctx{}; ctx.esp = 0x18000; ctx.ecx = 0x123456;
    Reset();
    Check(NativeControlsFunction(ctx, 0x140), "native Controls sync");
    Check(ctx.esp == 0x18000 && ctx.ecx == 0x123456, "sync preserves guest context");
    Check(captions[widgets["pc_controls_action0"]].find(u"W") != std::u16string::npos, "native row shows current binding");
    NativeControlsValue(0x150, std::bit_cast<uint32_t>(50.f));
    NativeControlsValue(0x151, std::bit_cast<uint32_t>(100.f));
    NativeControlsValue(0x152, 1);
    Check(x > 2.54f && x < 2.56f && y == 5.f && inverted, "native sliders independently change sensitivity and invert");
    // A row opens a native slot chooser; Primary and Alternate preserve one another.
    intptr_t result = 0;
    auto Choose = [&](uint32_t choice) { Close(choice); TickNativeControls(ctx); };
    auto Begin = [&](uint32_t slot) {
        NativeControlsFunction(ctx, 0x141); Choose(slot);
        Check(ControlsCapturing(), "native slot picker starts capture");
    };
    auto Key = [&](uint16_t key) {
        Check(ControlsMessage(0x100, key, 0, &result), "capture consumes keyboard event");
        ControlsMessage(0x101, key, 0, &result); TickNativeControls(ctx);
        Check(!ControlsCapturing(), "capture ends before conflict navigation");
    };
    auto Capture = [&](uint16_t key, unsigned slot = 0) { Begin(slot); Key(key); };
    Capture('S'); Choose(2);
    Check(Matches(*Get(), Action::MoveUp, 'W') && !Matches(*Get(), Action::MoveUp, 'S'), "conflict Cancel preserves mapping");
    Capture('S'); Choose(1);
    Check(Matches(*Get(), Action::MoveUp, 'S') && Matches(*Get(), Action::MoveDown, 'S'), "Keep both shares input");
    Reset(); Capture('S'); Choose(0);
    Check(Matches(*Get(), Action::MoveUp, 'S') && !Matches(*Get(), Action::MoveDown, 'S'), "Replace removes overlapping conflict");
    Capture('J', 1);
    Check(Matches(*Get(), Action::MoveUp, 'J') && Matches(*Get(), Action::MoveUp, 'S'), "alternate preserves primary");
    NativeControlsFunction(ctx, 0x141); Choose(2); Choose(0); Key('K');
    Check(Get()->inputs[size_t(Action::MoveUp)].size() == 3, "More Add preserves multiple bindings");
    Capture('L');
    Check(Matches(*Get(), Action::MoveUp, 'L') && Matches(*Get(), Action::MoveUp, 'J') && Matches(*Get(), Action::MoveUp, 'K') &&
          !Matches(*Get(), Action::MoveUp, 'S'), "editing primary preserves alternate and additional inputs");
    Begin(0); ControlsMessage(0x100, 27, 0, &result); TickNativeControls(ctx);
    Check(!ControlsCapturing() && Matches(*Get(), Action::MoveUp, 'L'), "Escape cancels capture");
    Check(ControlsMessage(0x100, 27, intptr_t(1) << 30, &result), "held capture-cancel key cannot reach underlying page");
    ControlsMessage(0x101, 27, 0, &result);
    Begin(0); ControlsMessage(0x8, 0, 0, &result); TickNativeControls(ctx);
    Check(!ControlsCapturing(), "focus loss cancels capture");
    NativeControlsFunction(ctx, 0x141); Choose(2); Choose(1); Choose(0);
    Check(Get()->inputs[size_t(Action::MoveUp)].empty(), "More Unbind confirmation");
    NativeControlsFunction(ctx, 0x148); Choose(2);
    Check(Get()->inputs[size_t(Action::MoveUp)].empty(), "Reset Cancel preserves mapping");
    NativeControlsFunction(ctx, 0x148); ControlsMessage(0x100, 27, 0, &result); TickNativeControls(ctx);
    Check(Get()->inputs[size_t(Action::MoveUp)].empty(), "Back cancels reset without applying selected button");
    ControlsMessage(0x101, 27, 0, &result);
    NativeControlsFunction(ctx, 0x148);
    input::Pad pad{}; pad.connected = true; pad.analog[1] = 255;
    ControlsPad(0, pad); TickNativeControls(ctx);
    pad.analog[1] = 255; ControlsPad(0, pad);
    Check(pad.analog[1] == 0, "held controller B stays consumed after native dialog closes");
    pad.analog[1] = 0; ControlsPad(0, pad);
    pad.analog[1] = 255; ControlsPad(0, pad);
    Check(pad.analog[1] == 255, "fresh B works again after release");
    NativeControlsFunction(ctx, 0x148); Choose(0);
    Check(Matches(*Get(), Action::MoveUp, 'W') && x > 2.5f && y == 5 && inverted, "keybind reset leaves mouse settings alone");
    NativeControlsFunction(ctx, 0x149); Choose(0);
    Check(x == 1 && y == 1 && !inverted && Matches(*Get(), Action::MoveUp, 'W'), "mouse reset restores only mouse defaults");
    Check(saved >= 7, "edits request config persistence");
    for (size_t a = 0; a < ActionCount; a++) {
        NativeControlsValue(0x154, uint32_t(a / 7)); TickNativeControls(ctx);
        std::string name = "pc_controls_action" + std::to_string(a % 7);
        auto label = captions[widgets[name]];
        std::string expected = Info(Action(a)).label;
        Check(label.find(std::u16string(expected.begin(), expected.end())) != std::u16string::npos, "every native action row is reachable");
        Check(label.find(u" | ") != std::u16string::npos, "primary and alternate fields appear together");
    }
    for (bool main : {false, true}) {
        auto script = NativeControlsScript(main ? "FEsubMenu_title" : "FEpause_title", main ? "FEsubMenu_text" : "FEpause_text", main, "\r\n");
        auto parent_start = script.find("AddScreen \"pc_controls\"");
        auto keys_start = script.find("AddScreen \"pc_keybinds\"");
        auto parent = script.substr(parent_start, keys_start - parent_start);
        auto keys = script.substr(keys_start, script.find("AddDialog", keys_start) - keys_start);
        Check(parent.find("gotoscreen \"pc_keybinds\"") != std::string::npos, "Controls opens its own native Keybinds page");
        Check(parent.find("pc_controls_action") == std::string::npos && keys.find("AddHSlider") == std::string::npos, "bindings list is separate from mouse settings");
        Check(parent.find("AddHSlider") != std::string::npos && script.find("AddDialog \"pc_controls_dialog\"") != std::string::npos, "native sliders and popup");
        Check(keys.find("pc_controls_action6") != std::string::npos && keys.find("TBS_PC_BIND_PRIMARY") == std::string::npos, "dedicated page has seven action rows");
        Check(script.find("ImGui") == std::string::npos, "no overlay renderer");
        Check(script.find(main ? "FEmain_adjustmentMETER2" : "FEpause_adjustmentMETER2") != std::string::npos, "separate native Audio meter locators");
    }
    std::printf("Native Controls: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
