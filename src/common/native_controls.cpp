#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "common/native_controls.h"
#include "common/addresses.h"
#include "common/bindings.h"
#include "common/display_menu.h"
#include "common/replay.h"
#include "app/app.h"
#include "core/log.h"
#include "core/input.h"
#include "kernel/kernel.h"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <optional>

namespace hulk {
namespace {
// Verified against both retail XBEs: the same APIs used by Audio Options and FEDoPushDialog/PopDialog.
#ifdef HULK_USA
constexpr uint32_t kRegisterFloat = 0xFC220, kSetSlider = 0xF1A70, kRawText = 0xF79A0;
constexpr uint32_t kShowDialog = 0xBE230, kCloseDialog = 0xE63A0;
#else
constexpr uint32_t kRegisterFloat = 0xFC440, kSetSlider = 0xF1C90, kRawText = 0xF7BC0;
constexpr uint32_t kShowDialog = 0xBE390, kCloseDialog = 0xE65C0;
#endif
constexpr uint32_t kSync = 0x140, kFirstRow = 0x141, kReset = 0x148, kMouseReset = 0x149;
constexpr uint32_t kX = 0x150, kY = 0x151, kInvert = 0x152, kPage = 0x154;
constexpr size_t kRows = 7, kPages = (bindings::ActionCount + kRows - 1) / kRows;
const char* kRowsNames[] = {"pc_controls_action0", "pc_controls_action1", "pc_controls_action2", "pc_controls_action3", "pc_controls_action4", "pc_controls_action5", "pc_controls_action6"};
ControlsMouseHooks mouse{};
enum class Dialog { None, ChooseSlot, More, Capture, Conflict, Reset, ResetMouse, Unbind };
Dialog dialog = Dialog::None; // guest frontend thread only
size_t page = 0;
int edit_slot = 0; // Primary (0), Alternate (1), or append (-1)
bindings::Action action{};
bindings::Input candidate{};
std::atomic<bool> capture{false}, refresh{false}, modal{false}, cancel_dialog{false};
std::atomic<int> pending{-1}; // -1 no event, -2 cancel, otherwise Input
std::atomic<int> fixture_command{-1};
std::atomic<uint16_t> last_pad{0};
std::atomic<bool> block_back_until_release{false};
std::atomic<int> blocked_window_input{-1};

std::optional<bindings::Input> MessageInput(uint32_t message, uintptr_t wp, intptr_t lp) {
    if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP || message == WM_SYSKEYUP)
        return bindings::KeyInput(uint32_t(wp), lp);
    if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) return VK_LBUTTON;
    if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP) return VK_RBUTTON;
    if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP) return VK_MBUTTON;
    if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP) return HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    return {};
}

uint32_t GuestString(X86Context& ctx, const std::string& text) {
    ctx.esp = (ctx.esp - uint32_t(text.size() + 8)) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), text.c_str(), text.size() + 1);
    return ctx.esp;
}
uint32_t Widget(X86Context& ctx, const char* name) {
    auto esp = ctx.esp;
    auto text = GuestString(ctx, name);
    auto widget = kernel::CallGuest(ctx, addr::kFindWidget, {text});
    ctx.esp = esp;
    return widget;
}
void Caption(X86Context& ctx, const char* name, const std::string& text) {
    auto widget = Widget(ctx, name);
    if (!widget) return;
    auto esp = ctx.esp;
    // FEObject's label is an FEString at +0x18. Its raw UTF-16 setter owns a copy.
    std::u16string wide(text.begin(), text.end());
    ctx.esp = (ctx.esp - uint32_t((wide.size() + 1) * 2 + 4)) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), wide.c_str(), (wide.size() + 1) * 2);
    ctx.ecx = widget + 0x18;
    kernel::CallGuest(ctx, kRawText, {ctx.esp, 0});
    ctx.esp = esp;
}
void Selection(X86Context& ctx, const char* name, uint32_t value, bool slider = false) {
    auto widget = Widget(ctx, name);
    if (!widget) return;
    ctx.ecx = widget;
    kernel::CallGuest(ctx, slider ? kSetSlider : kernel::R32(kernel::R32(widget) + 0x64), {value});
}
float Percent(float multiplier) { return (multiplier - 0.1f) * (100.f / 4.9f); }
void Sync(X86Context& ctx) {
    if (!mouse.get_x) return;
    Selection(ctx, "pc_controls_x", std::bit_cast<uint32_t>(Percent(mouse.get_x())), true);
    Selection(ctx, "pc_controls_y", std::bit_cast<uint32_t>(Percent(mouse.get_y())), true);
    Selection(ctx, "pc_controls_invert", mouse.get_invert_y() ? 1 : 0);
    Selection(ctx, "pc_controls_page", uint32_t(page));
    char label[96];
    std::snprintf(label, sizeof(label), "Mouse sensitivity X (%.2fx)", mouse.get_x());
    Caption(ctx, "pc_controls_x", label);
    std::snprintf(label, sizeof(label), "Mouse sensitivity Y (%.2fx)", mouse.get_y());
    Caption(ctx, "pc_controls_y", label);
    auto map = bindings::Get();
    for (size_t row = 0; row < kRows; row++) {
        size_t i = page * kRows + row;
        std::string label = "-";
        if (i < bindings::ActionCount) {
            auto a = bindings::Action(i);
            const auto& keys = map->inputs[i];
            label = std::string(bindings::Info(a).label) + ": " +
                (keys.empty() ? "Not bound" : bindings::InputName(keys[0])) + " | " +
                (keys.size() < 2 ? "Not bound" : bindings::InputName(keys[1]));
            if (keys.size() > 2) label += " (+" + std::to_string(keys.size() - 2) + ")";
        }
        Caption(ctx, kRowsNames[row], label);
    }
}
bool Show(X86Context& ctx, Dialog kind, const std::string& message,
          const char* left, const char* middle, const char* right, unsigned selection = 1) {
    auto manager = kernel::R32(addr::kFrontend);
    if (!manager || kernel::R32(manager + 0x2218)) return false;
    const auto esp = ctx.esp;
    auto name = GuestString(ctx, "pc_controls_dialog");
    auto msg = GuestString(ctx, "TBS_PC_BIND_MESSAGE");
    auto l = GuestString(ctx, left), m = GuestString(ctx, middle), r = GuestString(ctx, right);
    ctx.ecx = manager;
    bool shown = kernel::CallGuest(ctx, kShowDialog, {name, msg, l, m, r, selection, 0, 0}) != 0;
    ctx.esp = esp;
    if (shown) {
        dialog = kind;
        modal.store(true);
        Caption(ctx, "pc_controls_message", message);
    }
    return shown;
}
void Save() { app::Save(); refresh.store(true); }
void Commit(bool remove_conflicts) {
    auto map = *bindings::Get();
    auto keys = map.inputs[size_t(action)];
    bindings::Assign(map, action, candidate, true, remove_conflicts);
    if (edit_slot >= 0) {
        if (size_t(edit_slot) < keys.size()) keys[size_t(edit_slot)] = candidate;
        else keys.push_back(candidate);
        std::vector<bindings::Input> unique;
        for (auto key : keys) if (std::find(unique.begin(), unique.end(), key) == unique.end()) unique.push_back(key);
        map.inputs[size_t(action)] = std::move(unique);
    }
    bindings::Set(std::move(map));
    Save();
}
void CancelCapture() { pending.store(-2); }
} // namespace

void SetControlsMouseHooks(ControlsMouseHooks hooks) { mouse = hooks; }
bool ControlsCapturing() { return capture.load(); }
bool CaptureControlsInput(bindings::Input input) {
    if (!ControlsCapturing() || bindings::InputName(input).empty()) return false;
    int expected = -1;
    return pending.compare_exchange_strong(expected, input == VK_ESCAPE ? -2 : int(input));
}
void RequestControlsFixture(unsigned command) {
    if (BackgroundTest()) fixture_command.store(int(command));
}

std::string NativeControlsStrings() {
    std::string out =
        "TBS_PC_MOUSE_X\tMouse sensitivity X\r\nTBS_PC_MOUSE_Y\tMouse sensitivity Y\r\n"
        "TBS_PC_MOUSE_INVERT\tInvert mouse Y\r\nTBS_PC_BIND_MODE\tBinding action\r\n"
        "TBS_PC_BIND_PAGE\tActions (Primary / Alternate)\r\nTBS_PC_BIND_REPLACE\tReplace\r\nTBS_PC_BIND_ADD\tAdd\r\n"
        "TBS_PC_BIND_UNBIND\tUnbind\r\nTBS_PC_BIND_KEEP\tKeep both\r\nTBS_PC_BIND_CANCEL\tCancel\r\n"
        "TBS_PC_BIND_RESET\tReset keybinds to defaults\r\nTBS_PC_BIND_MESSAGE\tControls\r\n"
        "TBS_PC_KEYBINDS\tKeybinds\r\nTBS_PC_BIND_PRIMARY\tPrimary\r\nTBS_PC_BIND_ALTERNATE\tAlternate\r\n"
        "TBS_PC_BIND_MORE\tMore...\r\nTBS_PC_MOUSE_RESET\tReset mouse defaults\r\n";
    for (size_t i = 0; i < kPages; i++)
        out += "TBS_PC_BIND_PAGE" + std::to_string(i) + "\t" + std::to_string(i + 1) + " / " + std::to_string(kPages) + "\r\n";
    return out;
}

std::string NativeControlsScript(const char* title, const char* row, bool main, const std::string& nl) {
    // Two real native pages: mouse settings, then the dedicated bindings list.
    std::string s;
    for (auto axis : {"X", "Y"}) s += "AddNamedValueFloat \"PCMouse" + std::string(axis) + "\" 18.36735 \"PCMouse" + axis + "Changed\"" + nl;
    s += "AddNamedValueInt \"PCMouseInvert\" 0 \"PCMouseInvertChanged\"" + nl;
    s += "AddNamedValueInt \"PCBindingPage\" 0 \"PCBindingPageChanged\"" + nl;
    auto begin = [&](const char* name, const char* label) {
        s += "AddScreen \"" + std::string(name) + "\"" + nl + "{" + nl + "    $MACRO_INSERT typicalScreenSetup" + nl;
        s += "    AddLabel \"" + std::string(title) + "\"" + nl + "    {" + nl + "        text \"" + label + "\" 1" + nl +
             "        $MACRO_INSERT FormatTitleText" + nl + "        TransitionAnimEnter \"fadein\"" + nl +
             "        TransitionAnimExit \"fadeout\"" + nl + "    }" + nl;
    };
    auto format = [&](int n, bool selector = true) {
        std::string f = "        OnHighlight function \"PCControlsSync\"" + nl;
        f += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\" \"" +
             std::string(selector ? "2" : "1") + "\"" + nl;
        if (main) f += "        $MACRO_INSERT menu_item_" + std::to_string(n) + nl + "        halign left" + nl;
        else f += "        $MACRO_INSERT FormatText" + nl + "        SelectedDrawable \"FE_selectBar\"" + nl +
                  "        NotSelectedDrawable \"FE_backgroundBar\"" + nl + "        TransitionAnimEnter \"slide_down" +
                  std::to_string(n) + "\"" + nl + "        TransitionAnimExit \"slide_down" + std::to_string(n) + "\"" + nl;
        return f;
    };
    auto button = [&](int n, const char* label, const std::string& command, const char* name = nullptr) {
        s += "    AddTextButton \"" + std::string(row) + std::to_string(n) + "\"" + nl + "    {" + nl;
        s += "        text \"" + std::string(label) + "\" 1" + nl;
        if (name) s += "        name \"" + std::string(name) + "\"" + nl;
        s += "        OnSelect " + command + nl + format(n, false) + "    }" + nl;
    };
    begin("pc_controls", "TBS_PC_CONTROLS");
    button(1, "TBS_PC_KEYBINDS", "gotoscreen \"pc_keybinds\"");
    s += "    AddTextSelector \"" + std::string(row) + "2\"" + nl + "    {" + nl +
         "        text \"TBS_PC_MOUSE_INVERT\" 1" + nl + "        name \"pc_controls_invert\"" + nl +
         "        NamedValue \"PCMouseInvert\"" + nl + "        TextSelection \"\" TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES" + nl +
         format(2) + "    }" + nl;
    button(3, "TBS_PC_MOUSE_RESET", "function \"PCControlsMouseReset\"");
    for (int i = 0; i < 2; i++) {
        auto axis = i ? "Y" : "X";
        s += "    AddHSlider \"" + std::string(row) + std::to_string(i + 4) + "\"" + nl + "    {" + nl;
        s += "        text \"TBS_PC_MOUSE_" + std::string(axis) + "\" 1" + nl;
        s += "        name \"pc_controls_" + std::string(i ? "y" : "x") + "\"" + nl;
        s += "        NamedValue \"PCMouse" + std::string(axis) + "\"" + nl;
        s += "        step 2.0408163" + nl + "        default 18.36735" + nl + "        DecimalPlaces 0" + nl;
        s += "        Bar \"" + std::string(main ? "FEmain" : "FEpause") + "_adjustmentMeter\" 19" + nl;
        s += "        BarLocator \"" + std::string(main ? "FEmain" : "FEpause") + "_adjustmentMETER" + std::to_string(i + 1) + "\"" + nl;
        s += format(i + 4) + "    }" + nl;
    }
    s += "    $MACRO_INSERT SetupControlButtonLabel" + nl + "}" + nl;
    begin("pc_keybinds", "TBS_PC_KEYBINDS");
    s += "    AddTextSelector \"" + std::string(row) + "1\"" + nl + "    {" + nl +
         "        text \"TBS_PC_BIND_PAGE\" 1" + nl + "        name \"pc_controls_page\"" + nl +
         "        NamedValue \"PCBindingPage\"" + nl + "        TextSelection \"\"";
    for (size_t p = 0; p < kPages; p++) s += " TBS_PC_BIND_PAGE" + std::to_string(p);
    s += nl + format(1) + "    }" + nl;
    for (size_t i = 0; i < kRows; i++) button(int(i + 2), "TBS_PC_BIND_MESSAGE", "function \"PCControlsRow" + std::to_string(i) + "\"", kRowsNames[i]);
    button(9, "TBS_PC_BIND_RESET", "function \"PCControlsReset\"");
    s += "    $MACRO_INSERT SetupControlButtonLabel" + nl + "}" + nl;
    // The game's own FEGenericDialog template, with distinct names. No host UI or renderer.
    s += "AddDialog \"pc_controls_dialog\"" + nl + "{" + nl +
         "    AddSceneGraph \"FEpopUp_new\"" + nl + "    AddLocators \"FEpopUp_Locators\"" + nl;
    const char* positions[] = {"Left", "Middle", "Right"};
    for (int i = 0; i < 3; i++) {
        auto name = "pc_controls_dialog" + std::to_string(i);
        s += "    AddTextButton \"FEpopUp_item" + std::to_string(i + 1) + "\"" + nl + "    {" + nl;
        s += "        name \"" + name + "\"" + nl + "        text \"\" 0" + nl;
        if (main) s += "        $MACRO_INSERT FormatMenuItem" + nl;
        else s += "        halign center" + nl + "        SelectedDrawable \"FE_selectBar\"" + nl +
                  "        NotSelectedDrawable \"FE_backgroundBar\"" + nl;
        s += "        OnSelect popdialog" + nl + "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\" \"3\"" + nl;
        s += "        TransitionAnimEnter \"fadein\"" + nl + "        TransitionAnimExit \"fadeout\"" + nl + "    }" + nl;
        s += "    " + std::string(positions[i]) + "Button \"" + name + "\"" + nl;
    }
    s += "    AddLabel \"FEpopUp_text\"" + nl + "    {" + nl + "        name \"pc_controls_message\"" + nl +
         "        text \"TBS_PC_BIND_MESSAGE\" 1" + nl + "        halign center" + nl +
         (main ? std::string() : "        valign center" + nl) + "        LineWidth -1" + nl +
         "        TextSize 12" + nl + "        TransitionAnimEnter \"fadein\"" + nl + "        TransitionAnimExit \"fadeout\"" + nl +
         "    }" + nl + "    MessageLabel \"pc_controls_message\"" + nl + "}" + nl;
    return s;
}

void RegisterControlsCallbacks(X86Context& ctx) {
    const auto saved = ctx;
    const char* funcs[] = {"PCControlsSync", "PCControlsRow0", "PCControlsRow1", "PCControlsRow2", "PCControlsRow3", "PCControlsRow4", "PCControlsRow5", "PCControlsRow6", "PCControlsReset", "PCControlsMouseReset"};
    const char* values[] = {"PCMouseXChanged", "PCMouseYChanged", "PCMouseInvertChanged", "PCBindingPageChanged"};
    const uint32_t owners[] = {kX, kY, kInvert, kPage};
    for (unsigned i = 0; i < 14; i++) {
        ctx.esp = saved.esp;
        bool fn = i < 10;
        auto name = GuestString(ctx, fn ? funcs[i] : values[i - 10]);
        uint32_t f = kernel::CallGuest(ctx, addr::kGuestNew, {12});
        if (!f) break;
        kernel::W32(f, fn ? addr::kFunctorVtable : addr::kIntFunctorVtable);
        kernel::W32(f + 4, fn ? kSync + i : owners[i - 10]);
        kernel::W32(f + 8, fn ? addr::kSyncHandler : addr::kSharedHandler);
        kernel::CallGuest(ctx, fn ? addr::kRegisterCallback : i < 12 ? kRegisterFloat : addr::kRegisterIntCallback, {name, f});
    }
    ctx = saved;
}

bool NativeControlsFunction(X86Context& ctx, uint32_t owner) {
    if (owner < kSync || owner > kMouseReset) return false;
    const auto saved = ctx;
    if (owner == kSync) Sync(ctx);
    else if (owner == kReset) Show(ctx, Dialog::Reset, "Reset all keyboard/mouse keybinds?", "TBS_PC_BIND_RESET", "", "TBS_PC_BIND_CANCEL", 2);
    else if (owner == kMouseReset) Show(ctx, Dialog::ResetMouse, "Reset mouse sensitivity and inversion?", "TBS_PC_MOUSE_RESET", "", "TBS_PC_BIND_CANCEL", 2);
    else if (size_t i = page * kRows + owner - kFirstRow; i < bindings::ActionCount) {
        action = bindings::Action(i);
        const auto map = bindings::Get();
        const auto& keys = map->inputs[i];
        Show(ctx, Dialog::ChooseSlot, std::string(bindings::Info(action).label) + "\nPrimary: " +
             (keys.empty() ? "Not bound" : bindings::InputName(keys[0])) + "\nAlternate: " +
             (keys.size() < 2 ? "Not bound" : bindings::InputName(keys[1])),
             "TBS_PC_BIND_PRIMARY", "TBS_PC_BIND_ALTERNATE", "TBS_PC_BIND_MORE", 0);
    }
    ctx = saved;
    return true;
}

bool NativeControlsValue(uint32_t owner, uint32_t value) {
    if (owner != kX && owner != kY && owner != kInvert && owner != kPage) return false;
    if (owner == kX || owner == kY) {
        float p = std::bit_cast<float>(value);
        if (std::isfinite(p) && mouse.set_x) {
            float multiplier = 0.1f + std::clamp(p, 0.f, 100.f) * 0.049f;
            (owner == kX ? mouse.set_x : mouse.set_y)(multiplier);
            NoteSettingChanged();
        }
    } else if (owner == kInvert && value < 2 && mouse.set_invert_y) { mouse.set_invert_y(value != 0); NoteSettingChanged(); }
    else if (owner == kPage && value < kPages) page = value;
    refresh.store(true);
    return true;
}

void TickNativeControls(X86Context& ctx) {
    const auto saved = ctx;
    auto manager = kernel::R32(addr::kFrontend);
    if (!manager) return;
    if (BackgroundTest()) {
        static bool reported = false;
        if (!reported) { reported = true; LOGR("native Controls: guest frame tick active, frontend=%08X", manager); }
        unsigned cached = 0;
        for (unsigned i = 0; i < 64; i++) if (kernel::R32(manager + 0x0C + i * 4)) cached++;
        static unsigned last_cached = 65;
        if (cached != last_cached) { last_cached = cached; LOGR("native frontend: %u cached screens", cached); }
        if (int command = fixture_command.exchange(-1); command >= 0) {
            if (command < 4) {
                const char* screens[] = {"options", "display_options", "pc_controls", "pc_keybinds"};
                auto name = GuestString(ctx, screens[command]);
                ctx.ecx = manager;
                // FEDoScreen::Execute uses this same manager virtual, +0x38.
                kernel::CallGuest(ctx, kernel::R32(kernel::R32(manager) + 0x38), {name});
                refresh.store(command >= 2);
            } else if (command >= 10 && command <= 16) NativeControlsFunction(ctx, kFirstRow + command - 10);
            else if (command == 17) NativeControlsFunction(ctx, kReset);
            else if (command == 18) NativeControlsFunction(ctx, kMouseReset);
            else if (command >= 20 && command < 30) NativeControlsValue(kPage, command - 20);
            LOGR("native Controls fixture: command %d", command);
        }
    }
    if (cancel_dialog.exchange(false) && dialog != Dialog::None) {
        ctx.ecx = manager;
        kernel::CallGuest(ctx, kCloseDialog, {});
        dialog = Dialog::None;
        modal.store(false); capture.store(false); pending.store(-1);
        refresh.store(true);
    }
    if (dialog != Dialog::None && !kernel::R32(manager + 0x2218)) {
        auto choice = kernel::R32(manager + 0x221C);
        auto closed = dialog;
        dialog = Dialog::None;
        modal.store(false);
        capture.store(false); pending.store(-1);
        if (closed == Dialog::Conflict && choice < 2) Commit(choice == 0);
        if (closed == Dialog::Reset && choice == 0) { bindings::Reset(); Save(); }
        if (closed == Dialog::ResetMouse && choice == 0) {
            if (mouse.set_x) { mouse.set_x(1.f); mouse.set_y(1.f); mouse.set_invert_y(false); }
            Save();
        }
        bool begin_capture = false;
        if (closed == Dialog::ChooseSlot) {
            if (choice < 2) { edit_slot = int(choice); begin_capture = true; }
            else if (choice == 2) Show(ctx, Dialog::More, std::string(bindings::Info(action).label) + "\nCurrent: " + bindings::Label(*bindings::Get(), action),
                                     "TBS_PC_BIND_ADD", "TBS_PC_BIND_UNBIND", "TBS_PC_BIND_CANCEL", 2);
        }
        if (closed == Dialog::More) {
            if (choice == 0) { edit_slot = -1; begin_capture = true; }
            else if (choice == 1) Show(ctx, Dialog::Unbind, std::string("Unbind ") + bindings::Info(action).label + "?",
                                       "TBS_PC_BIND_UNBIND", "", "TBS_PC_BIND_CANCEL", 2);
        }
        if (begin_capture && Show(ctx, Dialog::Capture, std::string(bindings::Info(action).label) +
             (edit_slot == 0 ? " - Primary" : edit_slot == 1 ? " - Alternate" : " - Add input") +
             "\nPress a key or mouse button.\nEscape / controller B: Cancel", "", "TBS_PC_BIND_CANCEL", "")) {
            pending.store(-1); capture.store(true);
        }
        if (closed == Dialog::Unbind && choice == 0) {
            auto map = *bindings::Get(); map.inputs[size_t(action)].clear(); bindings::Set(std::move(map)); Save();
        }
    }
    if (dialog == Dialog::Capture) {
        int event = pending.exchange(-1);
        if (event != -1) {
            ctx.ecx = manager;
            kernel::CallGuest(ctx, kCloseDialog, {});
            capture.store(false); modal.store(false); dialog = Dialog::None;
            if (event >= 0) {
                candidate = bindings::Input(event);
                auto conflicts = bindings::Conflicts(*bindings::Get(), action, candidate);
                if (conflicts.empty()) Commit(false);
                else {
                    std::string message = bindings::InputName(candidate) + " is also bound to:";
                    for (size_t i = 0; i < std::min<size_t>(conflicts.size(), 3); i++)
                        message += std::string("\n") + bindings::Info(conflicts[i]).label;
                    if (conflicts.size() > 3) message += "\n(and " + std::to_string(conflicts.size() - 3) + " more)";
                    Show(ctx, Dialog::Conflict, message, "TBS_PC_BIND_REPLACE", "TBS_PC_BIND_KEEP", "TBS_PC_BIND_CANCEL", 2);
                }
            }
            refresh.store(true);
        }
    }
    if (refresh.exchange(false)) Sync(ctx);
    ctx = saved;
}

bool ControlsMessage(uint32_t message, uintptr_t wp, intptr_t lp, intptr_t* result) {
    auto event_input = MessageInput(message, wp, lp);
    const bool released = message == WM_KEYUP || message == WM_SYSKEYUP || message == WM_LBUTTONUP ||
        message == WM_RBUTTONUP || message == WM_MBUTTONUP || message == WM_XBUTTONUP;
    if (event_input && blocked_window_input.load() == int(*event_input)) {
        if (released) blocked_window_input.store(-1);
        if (result) *result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
        return true;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wp)) blocked_window_input.store(-1);
    if (!ControlsCapturing()) {
        if (!modal.load()) return false;
        bool cancel = (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) &&
            (wp == VK_ESCAPE || bindings::Matches(*bindings::Get(), bindings::Action::MenuBack, bindings::KeyInput(uint32_t(wp), lp)));
        cancel |= message == WM_RBUTTONDOWN;
        if (cancel) {
            if (event_input) blocked_window_input.store(*event_input);
            cancel_dialog.store(true); if (result) *result = 0;
        }
        return cancel;
    }
    if (message == WM_KILLFOCUS || (message == WM_ACTIVATEAPP && !wp)) CancelCapture();
    std::optional<bindings::Input> code;
    if ((message == WM_KEYDOWN || message == WM_SYSKEYDOWN) && !(lp & (intptr_t(1) << 30))) code = bindings::KeyInput(uint32_t(wp), lp);
    if (message == WM_LBUTTONDOWN) code = VK_LBUTTON;
    if (message == WM_RBUTTONDOWN) code = VK_RBUTTON;
    if (message == WM_MBUTTONDOWN) code = VK_MBUTTON;
    if (message == WM_XBUTTONDOWN) code = HIWORD(wp) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2;
    if (message == WM_MOUSEWHEEL && int16_t(HIWORD(wp))) code = int16_t(HIWORD(wp)) > 0 ? bindings::WheelUp : bindings::WheelDown;
    if (message == WM_MOUSEHWHEEL && int16_t(HIWORD(wp))) code = int16_t(HIWORD(wp)) > 0 ? bindings::WheelRight : bindings::WheelLeft;
    if (code && CaptureControlsInput(*code) && *code < 256) blocked_window_input.store(*code);
    // Leave WM_INPUT to DefWindowProc for the system's raw-input cleanup.
    bool consume = (message >= WM_KEYFIRST && message <= WM_KEYLAST) ||
        (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST);
    if (consume && result) *result = message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
    return consume;
}
void ControlsPad(int port, input::Pad& pad) {
    if (port != 0) return;
    uint16_t buttons = pad.buttons | (pad.analog[1] ? 0x200 : 0);
    const auto previous = last_pad.exchange(buttons);
    if (!(buttons & 0x200)) block_back_until_release.store(false);
    if (ControlsCapturing()) {
        if (buttons & 0x200) block_back_until_release.store(true);
        if ((buttons & 0x200) && !(previous & 0x200)) CancelCapture();
        pad = {}; pad.connected = true;
    } else if (modal.load() && (buttons & 0x200)) {
        if (!(previous & 0x200)) cancel_dialog.store(true);
        block_back_until_release.store(true);
        pad.analog[1] = 0;
    }
    if (block_back_until_release.load()) pad.analog[1] = 0;
    if (modal.load()) pad.buttons &= ~0x10; // keep Start from changing game flow behind a dialog
}
} // namespace hulk
