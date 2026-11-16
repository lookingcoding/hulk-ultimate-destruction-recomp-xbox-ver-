#include "common/bindings.h"
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <toml++/toml.hpp>
#ifdef HULK_CONTROLS_UI_TEST
#include "common/controls_ui.h"
#include "core/input.h"
#include <imgui.h>
#include <imgui_internal.h>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
namespace { int saves = 0, changes = 0; bool request = false; float sx=1, sy=1; bool invert=false; }
namespace app { void Save() { ++saves; } }
namespace hulk {
void NoteSettingChanged() { ++changes; }
bool TakeControlsRequest() { bool result=request; request=false; return result; }
}
#endif
namespace {
void Check(bool ok, const char* text) { if (!ok) { std::cerr << "FAIL: " << text << '\n'; std::exit(1); } }
}
int main() {
    using namespace hulk::bindings;
    const auto defaults = Defaults();
    const std::vector<std::vector<Input>> expected = {
        {'W'}, {'S'}, {'A'}, {'D'}, {38}, {40}, {37}, {39}, {27}, {9}, {'C'}, {'V'},
        {32,13}, {'Q'}, {1}, {'E'}, {'2'}, {'1'}, {2}, {16}, {32,1}, {'Q',2}, {46}, {13},
        {WheelUp,WheelLeft}, {WheelDown,WheelRight}, {WheelUp}, {WheelDown}
    };
    Check(expected.size() == ActionCount, "every action has a default expectation");
    for (size_t i=0; i<ActionCount; ++i) {
        Check(defaults.inputs[i] == expected[i], "preserve each original default/context");
        Check(!std::string(Info(Action(i)).label).empty(), "every action has a UI label");
        for (Input code : expected[i]) Check(Matches(defaults, Action(i), code), "default matches");
    }
    for (Input code=1; code<InputCount; ++code) {
        auto name = InputName(code);
        if (!name.empty()) Check(ParseInput(name) == code, "canonical key/mouse name round trip");
    }
    Check(KeyInput(16,0x00360000)==161 && KeyInput(16,0x002a0000)==160, "Shift side from scan code");
    Check(KeyInput(17,0x01000000)==163 && KeyInput(17,0)==162, "Ctrl side from extended flag");
    Check(KeyInput(18,0x01000000)==165 && KeyInput(18,0)==164, "Alt side from extended flag");
    Check(KeyInput('K',0)=='K' && KeyInput(999,0)==0, "ordinary/invalid virtual key");
    Check(ParseInput("lMb") == 1 && ParseInput("eSc") == 27 && ParseInput("f24") == 135, "aliases and case");
    Check(!ParseInput("Potato") && !ParseInput("") && !ParseInput("F25"), "unknown inputs rejected");
    auto table = toml::parse(R"(
[bindings]
move_up = ["I", "MouseX1", "i"]
move_down = []
move_left = "unbound"
move_right = ["L", "invalid key"]
jump = 7
attack = ["MouseMiddle", false]
menu_back = "Backspace"
zoom_in = "WheelDown"
dpad_left = { invalid = true }
)");
    Load(&table);
    auto loaded = Get();
    Check(loaded->inputs[size_t(Action::MoveUp)] == std::vector<Input>({'I',5}), "multiple inputs deduplicated");
    Check(loaded->inputs[size_t(Action::MoveDown)].empty() && loaded->inputs[size_t(Action::MoveLeft)].empty(), "explicit unbound");
    Check(loaded->inputs[size_t(Action::MoveRight)] == defaults.inputs[size_t(Action::MoveRight)], "invalid array falls back as a whole");
    Check(loaded->inputs[size_t(Action::Jump)] == defaults.inputs[size_t(Action::Jump)], "invalid scalar fallback");
    Check(loaded->inputs[size_t(Action::Attack)] == defaults.inputs[size_t(Action::Attack)], "mixed types fallback");
    Check(loaded->inputs[size_t(Action::DpadLeft)] == defaults.inputs[size_t(Action::DpadLeft)], "invalid table fallback");
    Check(Matches(*loaded, Action::MenuBack,8) && Matches(*loaded, Action::ZoomIn,WheelDown), "single string binding");
    Check(Label(*loaded, Action::MoveDown) == "Unbound", "unbound display");
    toml::table saved;
    saved.insert("unrelated", 42);
    Save(&saved);
    std::ostringstream serialized; serialized << saved;
    auto reparsed = toml::parse(serialized.str());
    Reset(); Load(&reparsed);
    Check(Get()->inputs == loaded->inputs, "TOML round trip preserves all actions and unbound");
    Check(saved["unrelated"].value<int>() == 42, "other settings preserved");
    Check(loaded->inputs[size_t(Action::MoveUp)] == std::vector<Input>({'I',5}), "old snapshot survives reset/reload");
    auto map = Defaults();
    Check(Conflicts(map,Action::MenuSelect,13).empty(), "menu select and gameplay Enter intentionally share");
    Check(Conflicts(map,Action::Grab,'Q').empty(), "game/menu B intentionally share");
    auto conflicts = Conflicts(map,Action::Attack,'Q');
    Check(conflicts == std::vector<Action>({Action::Grab}), "same-context conflict found");
    Assign(map,Action::Attack,'Q',false,true);
    Check(map.inputs[size_t(Action::Grab)].empty() && Matches(map,Action::MenuBack,'Q'), "replace removes conflicts only in overlapping contexts");
    Assign(map,Action::Attack,'K',true,false);
    Check(map.inputs[size_t(Action::Attack)] == std::vector<Input>({'Q','K'}), "append keeps multiple bindings");
    Check(Conflicts(map,Action::MoveUp,160) == std::vector<Action>({Action::Sprint}), "generic/left modifier conflict");
    Assign(map,Action::MoveUp,161,false,true);
    Check(map.inputs[size_t(Action::Sprint)].empty(), "replace removes overlapping generic modifier");
    Check(Matches(defaults,Action::Sprint,160) && Matches(defaults,Action::Sprint,161), "generic Shift accepts either side");
    Check(Matches(map,Action::MoveUp,161) && !Matches(map,Action::MoveUp,160) && !Matches(map,Action::MoveUp,16), "side-specific binding stays specific");
    InputState state{}; state['K']=true;
    Check(Down(map,Action::Attack,state) && !Down(map,Action::Jump,state), "snapshot action held query");
    Reset(); const auto before_invalid = Get();
    auto invalid = *before_invalid; invalid.inputs[0] = {0};
    bool threw=false; try { Set(invalid); } catch (const std::invalid_argument&) { threw=true; }
    Check(threw && Get() == before_invalid, "invalid publication cannot replace live snapshot");
    std::atomic<bool> good{true};
    std::thread reader([&] {
        for (int i=0;i<4000;++i) {
            auto snapshot=Get();
            auto key = snapshot->inputs[0][0];
            if ((key != 'W' && key != 'I') || snapshot->inputs[1][0] != (key == 'W' ? 'S' : 'K')) good=false;
        }
    });
    for (int i=0;i<1000;++i) { auto next=Defaults(); if(i%2) { next.inputs[0]={'I'}; next.inputs[1]={'K'}; } Set(std::move(next)); }
    reader.join(); Check(good, "reader sees complete maps while settings change");
    Load(nullptr); Check(Get()->inputs == defaults.inputs, "null load resets to defaults"); Save(nullptr);
    auto malformed=toml::parse("bindings = 12"); Load(&malformed);
    Check(Get()->inputs == defaults.inputs, "invalid bindings table fallback");
#ifdef HULK_CONTROLS_UI_TEST
    ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.DisplaySize=ImVec2(1280,960); io.DeltaTime=1.f/60;
    unsigned char* pixels; int width,height; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    hulk::SetControlsMouseHooks({[] {return sx;}, [](float v){sx=v;}, [] {return sy;}, [](float v){sy=v;}, [] {return invert;}, [](bool v){invert=v;}});
    auto frame=[] { ImGui::NewFrame(); hulk::DrawControls(60); ImGui::Render(); };
    Check(!hulk::ControlsOpen(), "panel initially closed");
    request=true; frame(); Check(hulk::ControlsOpen(), "native menu request opens host panel");
    intptr_t result=99;
    Check(hulk::ControlsMessage(WM_KEYDOWN,'K',0,&result) && result==0, "panel consumes game key input");
    Check(hulk::ControlsMessage(WM_INPUT,0,0,&result), "panel consumes raw mouse look");
    Check(!hulk::ControlsMessage(WM_SIZE,0,0,&result), "panel allows lifecycle messages");
    input::Pad pad{}; pad.connected=true; pad.buttons=8; pad.analog[0]=255;
    Check(hulk::ControlsPad(0,pad) && pad.connected && !pad.buttons && !pad.analog[0], "panel suppresses guest controller and retains connection");
    pad={}; hulk::ControlsPad(0,pad); frame();
    // Drive rendered widgets with synthetic ImGui events: no window, focus,
    // OS key polling, cursor movement, renderer, game or settings-file writes.
    auto click = [&](ImVec2 point) {
        io.AddMousePosEvent(point.x,point.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
    };
    auto first_row = [&] {
        for (auto window : ImGui::GetCurrentContext()->Windows)
            if (std::string(window->Name).find("/Actions_") != std::string::npos)
                return ImVec2(window->InnerRect.Min.x+20,window->InnerRect.Min.y+8);
        Check(false,"rendered action list exists"); return ImVec2();
    };
    click(first_row());
    hulk::ControlsMessage(WM_KEYDOWN,'I',0,&result); frame();
    Check(Matches(*Get(),Action::MoveUp,'I') && changes==1,"row click captures keyboard input and schedules save");
    frame(); click(first_row()); frame();
    auto* panel = ImGui::FindWindowByName("Controls / Keybinds");
    Check(panel != nullptr,"panel exists");
    // Cancel is the rendered button immediately before the action list.
    // Use live widget geometry rather than a desktop coordinate.
    ImGuiWindow* actions_window=nullptr;
    for (auto window : ImGui::GetCurrentContext()->Windows)
        if (std::string(window->Name).find("/Actions_") != std::string::npos) actions_window=window;
    Check(actions_window != nullptr,"rendered list exists during capture");
    ImVec2 cancel_point(actions_window->Pos.x+40,
        actions_window->Pos.y-ImGui::GetStyle().ItemSpacing.y-ImGui::GetFrameHeight()*.5f);
    io.AddMousePosEvent(cancel_point.x,cancel_point.y); frame();
    io.AddMouseButtonEvent(0,true);
    hulk::ControlsMessage(WM_LBUTTONDOWN,0,0,&result); frame();
    io.AddMouseButtonEvent(0,false); frame();
    Check(Get()->inputs[size_t(Action::MoveUp)]==std::vector<Input>({'I'}),"Cancel capture click does not become a MouseLeft binding");
    frame(); click(first_row());
    input::Pad cancel_pad{}; cancel_pad.analog[1]=255;
    hulk::ControlsPad(0,cancel_pad); frame(); frame();
    Check(hulk::ControlsOpen(),"controller B cancels capture without leaving panel");
    cancel_pad={}; hulk::ControlsPad(0,cancel_pad); frame(); frame();
    Check(Get()->inputs[size_t(Action::MoveUp)]==std::vector<Input>({'I'}),"controller cancel preserves binding");
    frame(); click(first_row());
    hulk::ControlsMessage(WM_XBUTTONDOWN,uintptr_t(1)<<16,0,&result); frame();
    Check(Get()->inputs[size_t(Action::MoveUp)]==std::vector<Input>({5}),"row captures extended mouse button");
    frame(); click(first_row());
    hulk::ControlsMessage(WM_KEYDOWN,'Q',0,&result); frame(); frame();
    auto* conflict=ImGui::FindWindowByName("Binding conflict");
    Check(conflict && conflict->Active && Get()->inputs[size_t(Action::MoveUp)]==std::vector<Input>({5}),
        "conflicting capture opens resolution dialog without changing bindings");
    // Cancel is the final button in the conflict dialog.
    click(ImVec2(conflict->WorkRect.Max.x-20,conflict->WorkRect.Max.y-ImGui::GetFrameHeight()*.5f));
    Check(Get()->inputs[size_t(Action::MoveUp)]==std::vector<Input>({5}),"cancel conflict preserves current binding");
    frame();
    for (auto window : ImGui::GetCurrentContext()->Windows)
        if (std::string(window->Name).find("/Actions_") != std::string::npos) actions_window=window;
    const auto& style=ImGui::GetStyle();
    const float unbind_width=ImGui::CalcTextSize("Unbind selected").x+style.FramePadding.x*2;
    sx=2; sy=3; invert=true;
    click(ImVec2(actions_window->Pos.x+unbind_width+style.ItemSpacing.x+30,
        actions_window->Pos.y+actions_window->Size.y+style.ItemSpacing.y+ImGui::GetFrameHeight()*.5f));
    frame();
    auto* reset=ImGui::FindWindowByName("Reset controls?");
    Check(reset && reset->Active,"reset requires explicit confirmation");
    click(ImVec2(reset->WorkRect.Min.x+20,reset->WorkRect.Max.y-ImGui::GetFrameHeight()*.5f));
    Check(Get()->inputs==Defaults().inputs && sx==1 && sy==1 && !invert,
        "reset restores every binding, both axes, and inversion");
    frame();
    io.AddKeyEvent(ImGuiKey_Escape,true); frame(); frame();
    Check(!hulk::ControlsOpen() && saves==1, "Escape closes panel and saves");
    Check(!hulk::ControlsMessage(WM_KEYDOWN,'K',0,&result), "closed panel does not consume keys");
    pad.buttons=4; Check(!hulk::ControlsPad(0,pad) && pad.buttons==4, "closed panel preserves physical controller");
    io.AddKeyEvent(ImGuiKey_Escape,false); frame(); ImGui::DestroyContext();
#endif
    std::cout << "bindings: all checks passed\n";
}
