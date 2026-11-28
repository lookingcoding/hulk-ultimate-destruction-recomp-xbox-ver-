#pragma once
#include <cstddef>
#include <cstdint>

struct X86Context;

// Mission / Free-Roam quick select (issue #26), all in the game's own Dolly frontend. The frontend keeps one list of
// the missions its StartMission will start (AddValidMission, 0x000B4C30; 0x80 entries at +0x26C, the id copied
// beside each at +0x6C); StartMission (0x000BC770, thiscall mode, id, 1, 1) refuses any id not in it. The host fills
// the list with every mission the disc ships and then starts the one the player picked: the disc's own
// "mission_selector" on fe_real.txt's main menu, and the "pc_missions" screen this adds to the pause menu.
namespace hulk {

// a mission: the directory it lives in (art\missions\<id>\<id>.dh) and the string table key that names it
struct Mission {
    const char* id;
    const char* name_key;  // TBS_C<chapter>M<mission>_NAME
};

inline constexpr size_t kStoryMissionCount = 35;    // the story: c00m01 .. c07m03
inline constexpr size_t kFreeRoamMissionCount = 40; // challenge missions: c10m01 .. c10m45
inline constexpr size_t kMissionCount = kStoryMissionCount + kFreeRoamMissionCount;
extern const Mission kStoryMissions[kStoryMissionCount];
extern const Mission kFreeRoamMissions[kFreeRoamMissionCount];

// a TextSelector's chosen index: its vtable +0x64 setter (0x000E32E0) writes the member at +0x154
inline constexpr uint32_t kSelectorSelection = 0x154;
inline constexpr uint32_t kSelectorSetSelection = 0x64;  // vtable offset of that setter

// the mission callbacks' owners, past the display options' (0x100..0x109), the cheats' (0x11F..0x127) and the
// controls' (0x140..0x154)
inline constexpr uint32_t kMissionSyncOwner = 0x170;    // PCMissionSync: the pc_missions selector's highlight
inline constexpr uint32_t kMissionSelectOwner = 0x171;  // PCMissionSelect: A on the pc_missions selector
inline constexpr uint32_t kMissionStartOwner = 0x172;   // PCMissionStart: A on fe_real.txt's mission_selector
inline constexpr uint32_t kMissionPageOwner = 0x173;    // PCMissionPageChanged: Story / Free Roam
inline constexpr uint32_t kMissionSelectedOwner = 0x174;  // PCMissionSelectedChanged: the chosen row

// puts every mission in the frontend's valid list, once per frontend (the manager kFrontend points at)
void AddValidMissions(X86Context& ctx);
// starts the mission the named TextSelector of the current screen has chosen, through the frontend's StartMission
bool StartSelectedMission(X86Context& ctx, const char* selector);
// the screen's function callbacks through the shared handler: true when it was one of theirs
bool MissionCallback(X86Context& ctx, uint32_t owner);
// the screen's int callbacks through the shared handler: true when it was one of theirs
bool MissionValue(X86Context& ctx, uint32_t owner, uint32_t value);

}  // namespace hulk
