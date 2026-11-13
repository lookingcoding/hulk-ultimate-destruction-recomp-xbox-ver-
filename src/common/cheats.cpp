#include "common/cheats.h"
#include <atomic>
#include "common/display_menu.h"
#include "core/log.h"

namespace hulk {
namespace { std::atomic<int> g_invulnerability{-1}; std::atomic<bool> g_screen_active{false}; }

int InvulnerabilityPreference() { return g_invulnerability.load(); }
void LoadInvulnerabilityPreference(int value) {
    g_invulnerability.store(value == 0 || value == 1 ? value : -1);
}
void InvulnerabilitySelected(bool enabled) {
    int value = enabled ? 1 : 0;
    if (g_invulnerability.exchange(value) == value) return;
    LOGR("cheats: invulnerability %s", enabled ? "on" : "off");
    NoteSettingChanged();
}
void SetCheatScreenActive(bool active) { g_screen_active.store(active); }
bool CheatScreenActive() { return g_screen_active.load(); }
}
