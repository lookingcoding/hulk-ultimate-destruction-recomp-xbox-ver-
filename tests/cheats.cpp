#include "common/cheats.h"
#include "common/display_menu.h"
#include "common/language.h"
#include "common/settings_ui.h"
#include "core/log.h"
#include <cstdio>
#include <toml++/toml.hpp>

namespace hulk {
int changes = 0; void NoteSettingChanged() { ++changes; }
void SetMouseSensitivity(float) {} float GetMouseSensitivity() { return 1; }
void SetMouseInvertY(bool) {} bool GetMouseInvertY() { return false; }
void SetMouseSmoothing(bool) {} bool GetMouseSmoothing() { return false; }
void SetCameraInvert(bool, bool) {} bool GetCameraInvertX() { return false; } bool GetCameraInvertY() { return false; }
void SetPromptDevice(PromptDevice) {} PromptDevice GetPromptDevice() { return PromptDevice::Auto; }
const char* PromptDeviceName(PromptDevice) { return "auto"; }
void SetFovIncrease(int) {} int GetFovIncrease() { return 0; }
void SetRenderDistance(int) {} int GetRenderDistance() { return 100; }
void SetCameraDistance(int) {} int GetCameraDistance() { return 100; }
void SetLanguage(Language) {} Language GetLanguage() { return Language::English; }
const char* LanguageName(Language) { return "english"; }
bool LanguageFromName(const char*, Language&) { return false; }
}
namespace logx { void Write(Level, const char*, ...) {} }

int main() {
    using namespace hulk;
    if (InvulnerabilityPreference() != -1) return 1;
    LoadInvulnerabilityPreference(8);
    if (InvulnerabilityPreference() != -1 || changes) return 2;
    InvulnerabilitySelected(true);
    if (InvulnerabilityPreference() != 1 || changes != 1) return 3;
    InvulnerabilitySelected(true);
    if (changes != 1) return 4;
    InvulnerabilitySelected(false);
    if (InvulnerabilityPreference() != 0 || changes != 2) return 5;
    LoadInvulnerabilityPreference(1);
    if (InvulnerabilityPreference() != 1 || changes != 2) return 6;
    LoadInvulnerabilityPreference(-1);
    if (InvulnerabilityPreference() != -1 || changes != 2) return 7;
    auto empty = toml::parse("");
    LoadCustomSettings(&empty);
    toml::table out;
    SaveCustomSettings(&out);
    if (InvulnerabilityPreference() != -1 || out.contains("cheat_invulnerability")) return 8;
    auto on = toml::parse("cheat_invulnerability = true");
    LoadCustomSettings(&on);
    SaveCustomSettings(&out);
    if (InvulnerabilityPreference() != 1 || out["cheat_invulnerability"].value_or(false) != true) return 9;
    auto off = toml::parse("cheat_invulnerability = false");
    LoadCustomSettings(&off);
    SaveCustomSettings(&out);
    if (InvulnerabilityPreference() != 0 || out["cheat_invulnerability"].value_or(true) != false) return 10;
    auto invalid = toml::parse("cheat_invulnerability = 'on'");
    LoadCustomSettings(&invalid);
    if (InvulnerabilityPreference() != -1) return 11;
    std::puts("cheat preference passed");
    return 0;
}
