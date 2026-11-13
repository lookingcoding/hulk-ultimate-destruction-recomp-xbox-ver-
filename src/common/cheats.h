#pragma once

namespace hulk {
// -1 means the player has not chosen a PC preference; otherwise 0 is off and 1 is on.
int InvulnerabilityPreference();
void LoadInvulnerabilityPreference(int value);
void InvulnerabilitySelected(bool enabled);
void SetCheatScreenActive(bool active);
bool CheatScreenActive();
}
