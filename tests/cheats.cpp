// Cheats (issue #12): levels, settings.toml round trips and the applier registry, without the game.
#include "common/cheats.h"
#include "common/display_menu.h"
#include "common/language.h"
#include "common/settings_ui.h"
#include "core/log.h"
#include "cpu/x86_context.h"
#include <cstdio>
#include <cstring>
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

namespace {
int failures = 0;
void Check(bool ok, const char* what) {
    std::printf("%s: %s\n", ok ? "ok" : "FAILED", what);
    if (!ok) failures++;
}
int applied = 0;
hulk::cheats::CheatState seen;
void Count(const hulk::cheats::CheatState& state, X86Context&, uint8_t*) { applied++; seen = state; }
}

int main() {
    using namespace hulk;
    using cheats::Cheat;
    Check(!cheats::Current().Any(), "every cheat starts off");
    for (size_t i = 0; i < cheats::kCheatCount; i++) {
        const auto& info = cheats::Info(Cheat(i));
        Check(size_t(info.cheat) == i && info.levels >= 2 && std::strncmp(info.key, "cheat_", 6) == 0 &&
                  cheats::FindInfo(info.key) == &info, info.key);
    }
    Check(!cheats::FindInfo("cheat_unknown") && !cheats::FindInfo(nullptr), "unknown keys are not cheats");

    Check(cheats::Select(Cheat::InfiniteRage, 1) && changes == 1 && cheats::Level(Cheat::InfiniteRage) == 1,
          "selecting a cheat stores it and saves the settings");
    Check(!cheats::Select(Cheat::InfiniteRage, 1) && changes == 1, "selecting the same level changes nothing");
    Check(cheats::Select(Cheat::Damage, 9) && cheats::Level(Cheat::Damage) == 3, "levels are clamped to the cheat's");
    Check(cheats::Select(Cheat::Damage, -4) && cheats::Level(Cheat::Damage) == 0, "negative levels are off");
    cheats::Load(Cheat::SuperJump, 1);
    Check(cheats::Current()[Cheat::SuperJump] == 1 && changes == 3, "loading does not mark the settings changed");

    // settings.toml: toggles as bools, levelled cheats as integers, missing or invalid as off
    auto in = toml::parse("cheat_invulnerability = true\ncheat_damage = 2\ncheat_game_speed = 99\n"
                          "cheat_regeneration = 'on'\ncheat_super_speed = 1\n");
    LoadCustomSettings(&in);
    const auto state = cheats::Current();
    Check(state[Cheat::Invulnerability] == 1 && state[Cheat::Damage] == 2 && state[Cheat::GameSpeed] == 3,
          "settings load booleans, levels and clamp levels");
    Check(state[Cheat::Regeneration] == 0 && state[Cheat::InfiniteRage] == 0 && state[Cheat::SuperJump] == 0,
          "missing or invalid keys load as off");
    Check(state[Cheat::SuperSpeed] == 1, "an integer 1 loads a toggle as on");
    toml::table out;
    SaveCustomSettings(&out);
    Check(out["cheat_invulnerability"].value<bool>() == true && out["cheat_damage"].value<int64_t>() == 2 &&
              out["cheat_game_speed"].value<int64_t>() == 3 && out["cheat_regeneration"].value<bool>() == false &&
              out["cheat_super_speed"].value<bool>() == true,
          "settings save toggles as bools and levels as integers");
    for (size_t i = 0; i < cheats::kCheatCount; i++)
        Check(out.contains(cheats::Info(Cheat(i)).key), "every cheat is saved");
    auto empty = toml::parse("");
    LoadCustomSettings(&empty);
    Check(!cheats::Current().Any(), "a settings file without cheats turns them all off");

    // appliers run with the current state, also when every cheat is off
    Check(cheats::RegisterApplier("count", &Count) && !cheats::RegisterApplier("none", nullptr), "appliers register");
    X86Context ctx{};
    cheats::RunAppliers(ctx, nullptr);
    Check(applied == 1 && !seen.Any(), "an applier sees every cheat off");
    cheats::Load(Cheat::LowGravity, 1);
    cheats::RunAppliers(ctx, nullptr);
    Check(applied == 2 && seen.On(Cheat::LowGravity), "an applier sees the current levels");

    std::printf("%s\n", failures ? "cheats FAILED" : "cheats passed");
    return failures ? 1 : 0;
}
