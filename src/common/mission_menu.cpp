#include "common/mission_menu.h"
#include "common/addresses.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include <cstring>

namespace hulk {
namespace {

// the two selectors the pause menu's mission screen names (fe_run.txt, display_menu.cpp)
constexpr const char* kPauseMissionSelector = "pc_mission_selector";
constexpr const char* kPausePageSelector = "pc_mission_page";
// the disc's own mission picker on the main menu (fe_real.txt)
constexpr const char* kMainMissionSelector = "mission_selector";

int page = 0;  // 0 story, 1 free roam: the frontend thread only

// a widget of the current screen by name, with the guest's stack as it was
uint32_t Widget(X86Context& ctx, const char* name) {
    const uint32_t esp = ctx.esp;
    ctx.esp = (esp - 64) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), name, std::strlen(name) + 1);
    const uint32_t widget = kernel::CallGuest(ctx, addr::kFindWidget, {ctx.esp});
    ctx.esp = esp;
    return widget;
}

// the TextSelector the widget is, null for any other widget
uint32_t Selector(X86Context& ctx, uint32_t widget) {
    if (!widget) return 0;
    const uint32_t esp = ctx.esp;
    ctx.esp = (esp - 32) & ~3u;
    const uint32_t selector =
        kernel::CallGuest(ctx, addr::kDynamicCast, {widget, 0, addr::kWidgetType, addr::kTextSelectorType, 0});
    ctx.esp = esp;
    return selector;
}

// the mission of an index in the screen's selector: story first, then free roam (null for none)
const Mission* MissionOf(int index) {
    if (index < 0) return nullptr;
    if (size_t(index) < kStoryMissionCount) return &kStoryMissions[index];
    if (size_t(index) < kMissionCount) return &kFreeRoamMissions[size_t(index) - kStoryMissionCount];
    return nullptr;
}

// the chosen index of a selector of the current screen (-1 when it is not there)
int Chosen(X86Context& ctx, const char* name) {
    const uint32_t selector = Selector(ctx, Widget(ctx, name));
    if (!selector) return -1;
    return int(kernel::R32(selector + kSelectorSelection));
}

void Choose(X86Context& ctx, const char* name, uint32_t index) {
    const uint32_t selector = Selector(ctx, Widget(ctx, name));
    if (!selector) return;
    const uint32_t ecx = ctx.ecx;
    ctx.ecx = selector;
    kernel::CallGuest(ctx, kernel::R32(kernel::R32(selector) + kSelectorSetSelection), {index});
    ctx.ecx = ecx;
}

void SetPage(X86Context& ctx, int value) {
    page = value ? 1 : 0;
    Choose(ctx, kPausePageSelector, uint32_t(page));
}

}  // namespace

const Mission kStoryMissions[kStoryMissionCount] = {
    {"c00m01", "TBS_C0M1_NAME"},  {"c00m02", "TBS_C0M2_NAME"},  {"c00m10", "TBS_C0M10_NAME"},
    {"c01m00", "TBS_C1M0_NAME"},  {"c01m01", "TBS_C1M1_NAME"},  {"c01m02", "TBS_C1M2_NAME"},
    {"c01m03", "TBS_C1M3_NAME"},  {"c01m04", "TBS_C1M4_NAME"},  {"c01m05", "TBS_C1M5_NAME"},
    {"c02m00", "TBS_C2M0_NAME"},  {"c02m01", "TBS_C2M1_NAME"},  {"c02m02", "TBS_C2M2_NAME"},
    {"c02m03", "TBS_C2M3_NAME"},  {"c02m04", "TBS_C2M4_NAME"},  {"c03m00", "TBS_C3M0_NAME"},
    {"c03m02", "TBS_C3M2_NAME"},  {"c03m03", "TBS_C3M3_NAME"},  {"c03m04", "TBS_C3M4_NAME"},
    {"c04m00", "TBS_C4M0_NAME"},  {"c04m01", "TBS_C4M1_NAME"},  {"c04m02", "TBS_C4M2_NAME"},
    {"c04m03", "TBS_C4M3_NAME"},  {"c04m04", "TBS_C4M4_NAME"},  {"c04m05", "TBS_C4M5_NAME"},
    {"c05m00", "TBS_C5M0_NAME"},  {"c05m01", "TBS_C5M1_NAME"},  {"c05m04", "TBS_C5M4_NAME"},
    {"c06m00", "TBS_C6M0_NAME"},  {"c06m01", "TBS_C6M1_NAME"},  {"c06m02", "TBS_C6M2_NAME"},
    {"c06m03", "TBS_C6M3_NAME"},  {"c07m00", "TBS_C7M0_NAME"},  {"c07m01", "TBS_C7M1_NAME"},
    {"c07m02", "TBS_C7M2_NAME"},  {"c07m03", "TBS_C7M3_NAME"},
};

const Mission kFreeRoamMissions[kFreeRoamMissionCount] = {
    {"c10m01", "TBS_C10M1_NAME"},   {"c10m02", "TBS_C10M2_NAME"},   {"c10m03", "TBS_C10M3_NAME"},
    {"c10m04", "TBS_C10M4_NAME"},   {"c10m05", "TBS_C10M5_NAME"},   {"c10m06", "TBS_C10M6_NAME"},
    {"c10m07", "TBS_C10M7_NAME"},   {"c10m08", "TBS_C10M8_NAME"},   {"c10m09", "TBS_C10M9_NAME"},
    {"c10m10", "TBS_C10M10_NAME"},  {"c10m11", "TBS_C10M11_NAME"},  {"c10m12", "TBS_C10M12_NAME"},
    {"c10m13", "TBS_C10M13_NAME"},  {"c10m14", "TBS_C10M14_NAME"},  {"c10m15", "TBS_C10M15_NAME"},
    {"c10m16", "TBS_C10M16_NAME"},  {"c10m17", "TBS_C10M17_NAME"},  {"c10m19", "TBS_C10M19_NAME"},
    {"c10m20", "TBS_C10M20_NAME"},  {"c10m21", "TBS_C10M21_NAME"},  {"c10m22", "TBS_C10M22_NAME"},
    {"c10m23", "TBS_C10M23_NAME"},  {"c10m24", "TBS_C10M24_NAME"},  {"c10m25", "TBS_C10M25_NAME"},
    {"c10m26", "TBS_C10M26_NAME"},  {"c10m27", "TBS_C10M27_NAME"},  {"c10m28", "TBS_C10M28_NAME"},
    {"c10m29", "TBS_C10M29_NAME"},  {"c10m30", "TBS_C10M30_NAME"},  {"c10m31", "TBS_C10M31_NAME"},
    {"c10m32", "TBS_C10M32_NAME"},  {"c10m34", "TBS_C10M34_NAME"},  {"c10m35", "TBS_C10M35_NAME"},
    {"c10m37", "TBS_C10M37_NAME"},  {"c10m38", "TBS_C10M38_NAME"},  {"c10m39", "TBS_C10M39_NAME"},
    {"c10m40", "TBS_C10M40_NAME"},  {"c10m41", "TBS_C10M41_NAME"},  {"c10m42", "TBS_C10M42_NAME"},
    {"c10m45", "TBS_C10M45_NAME"},
};

void AddValidMissions(X86Context& ctx) {
    const uint32_t manager = kernel::R32(addr::kFrontend);
    if (!manager) return;
    const uint32_t esp = ctx.esp, ecx = ctx.ecx;
    auto add = [&](const Mission& m) {
        // the id and the level its entry names (the mission's own directory) live on the guest's stack for the call
        ctx.esp = (esp - 64) & ~3u;
        const size_t length = std::strlen(m.id) + 1;
        if (length > 60) return;
        std::memcpy(kernel::HostPtr(ctx.esp), m.id, length);
        ctx.ecx = manager;
        kernel::CallGuest(ctx, addr::kAddValidMission, {ctx.esp, ctx.esp});
    };
    for (const Mission& m : kStoryMissions) add(m);
    for (const Mission& m : kFreeRoamMissions) add(m);
    ctx.esp = esp;
    ctx.ecx = ecx;
    static bool noted = false;
    if (!noted) {
        noted = true;
        LOGI("mission select: the frontend's valid list has the %zu story and %zu challenge missions",
             kStoryMissionCount, kFreeRoamMissionCount);
    }
}

bool StartSelectedMission(X86Context& ctx, const char* selector) {
    const uint32_t manager = kernel::R32(addr::kFrontend);
    const int index = Chosen(ctx, selector);
    const Mission* mission = MissionOf(index);
    if (!manager || index < 0 || !mission) return false;
    const uint32_t esp = ctx.esp, ecx = ctx.ecx;
    // "oneshot" and the mission's id, below the guest's stack: StartMission copies neither and reads both at once
    ctx.esp = (esp - 0x100) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), "oneshot", 8);
    const uint32_t mode = ctx.esp;
    ctx.esp = (ctx.esp - 64) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), mission->id, std::strlen(mission->id) + 1);
    const uint32_t id = ctx.esp;
    ctx.ecx = manager;
    kernel::CallGuest(ctx, addr::kStartMission, {mode, id, 1, 1});
    ctx.esp = esp;
    ctx.ecx = ecx;
    LOGI("mission select: %s starts %s (row %d of %s)", selector, mission->id, index,
         index < int(kStoryMissionCount) ? "the story" : "the challenge missions");
    return true;
}

bool MissionCallback(X86Context& ctx, uint32_t owner) {
    switch (owner) {
        case kMissionStartOwner:
            AddValidMissions(ctx);  // a frontend made since the callbacks were registered
            StartSelectedMission(ctx, kMainMissionSelector);
            return true;
        case kMissionSelectOwner:
            StartSelectedMission(ctx, kPauseMissionSelector);
            return true;
        case kMissionSyncOwner: {
            // the row the player highlighted decides the page the toggle shows
            const int index = Chosen(ctx, kPauseMissionSelector);
            if (index >= 0) SetPage(ctx, index >= int(kStoryMissionCount) ? 1 : 0);
            return true;
        }
        default:
            return false;
    }
}

bool MissionValue(X86Context& ctx, uint32_t owner, uint32_t value) {
    if (owner == kMissionPageOwner) {
        // Story 35 then Free Roam 40: the toggle moves the list to that page's first row
        SetPage(ctx, int(value));
        Choose(ctx, kPauseMissionSelector, page ? uint32_t(kStoryMissionCount) : 0);
        return true;
    }
    if (owner == kMissionSelectedOwner) {
        // the row changed: keep the toggle on the page it belongs to
        if (value < kMissionCount) SetPage(ctx, value >= kStoryMissionCount ? 1 : 0);
        return true;
    }
    return false;
}

}  // namespace hulk
