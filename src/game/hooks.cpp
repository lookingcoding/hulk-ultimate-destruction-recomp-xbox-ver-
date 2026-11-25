// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include <bit>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include "common/actor_owner_guard.h"
#include "common/addresses.h"
#include "common/camera.h"
#include "common/cheats.h"
#include "common/display_menu.h"
#include "common/native_controls.h"
#include "common/native_screen.h"
#include "common/native_hints.h"
#include "common/binding_prompts.h"
#include "common/language.h"
#include "common/simulation.h"
#include "common/view.h"
#include "core/log.h"
#include "cpu/x86_ops.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// Callbacks during the actor update can clear its owner after the initial check. Skip the owner's optional
// attachment lookup and finish the guest's cleanup, only when the owner the guest is about to read through is null:
// the plan comes from the guest's own instructions at the hook site (common/actor_owner_guard.h), not from eax.
bool HasNoActorUpdateOwner(X86Context& ctx, uint8_t* base) {
    static const hulk::OwnerGuardPlan plan = [base] {
        const uint8_t* code = base + hulk::addr::kActorOwnerGuard;
        const hulk::OwnerGuardPlan p = hulk::PlanOwnerGuard(code, 16);
        char bytes[16 * 3 + 1] = {};
        for (int i = 0; i < 16; ++i) std::snprintf(bytes + i * 3, 4, "%02X ", code[i]);
        static constexpr const char* kRules[] = {"unknown, testing eax", "owner register", "owner load"};
        if (p.rule == hulk::OwnerGuardRule::Unknown)
            LOGW("actor owner guard: %08X %s: %s", hulk::addr::kActorOwnerGuard, bytes, kRules[0]);
        else
            LOGI("actor owner guard: %08X %s: %s, reg %u base %d disp %08X", hulk::addr::kActorOwnerGuard, bytes,
                 kRules[unsigned(p.rule)], unsigned(p.owner_reg), int(p.load_base), uint32_t(p.load_disp));
        return p;
    }();
    uint32_t regs[8] = {ctx.eax, ctx.ecx, ctx.edx, ctx.ebx, ctx.esp, ctx.ebp, ctx.esi, ctx.edi};
    if (!hulk::OwnerGuardSkips(plan, regs, [base](uint32_t a) -> uint32_t { return X86_MEM32(a); })) return false;
    ctx.eax = regs[0], ctx.ecx = regs[1], ctx.edx = regs[2], ctx.ebx = regs[3];
    ctx.esp = regs[4], ctx.ebp = regs[5], ctx.esi = regs[6], ctx.edi = regs[7];
    LOG_FIRST(8, "actor owner guard: the owner is gone, finishing the update's cleanup");
    return true;
}

// the frontend's callbacks object (thiscall, owner; ret 4): registers each function the FE scripts name, then the
// display options' selectors (display_menu.cpp)
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_CALLBACKS));
X86_FUNC(HULK_SUB_FE_CALLBACKS) {
    HULK_IMP(HULK_SUB_FE_CALLBACKS)(ctx, base);
    hulk::RegisterDisplayCallbacks(ctx);
}

// Cached menu footer strings bypass FEString layout; resolve them before native measurement/drawing too.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_NATIVE_TEXT_DRAW));
X86_FUNC(HULK_SUB_NATIVE_TEXT_DRAW) {
    const uint32_t displacement = hulk::PrepareNativePromptDraw(ctx);
    struct RestoreStack {
        X86Context& ctx; uint32_t displacement;
        ~RestoreStack() { ctx.esp += displacement; }
    } restore{ctx, displacement};
    HULK_IMP(HULK_SUB_NATIVE_TEXT_DRAW)(ctx, base);
}

// Resolve cached semantic hint text before the game's own wrapping, measuring and drawing.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_TEXT_LAYOUT));
X86_FUNC(HULK_SUB_FE_TEXT_LAYOUT) {
    const uint32_t owner = ctx.ecx;
    struct Restore {
        uint32_t owner, raw;
        ~Restore() { kernel::W32(owner + 0x10, raw); }
    } restore{owner, kernel::R32(owner + 0x10)};
    hulk::PrepareNativeHint(ctx);
    HULK_IMP(HULK_SUB_FE_TEXT_LAYOUT)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_TEXT_DESTROY));
X86_FUNC(HULK_SUB_FE_TEXT_DESTROY) {
    hulk::ReleaseNativeHint(ctx);
    HULK_IMP(HULK_SUB_FE_TEXT_DESTROY)(ctx, base);
}

// The dedicated Keybinds page is a native FEMenu, beyond the retail 64-slot cache.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_SCREEN_CREATE));
X86_FUNC(HULK_SUB_FE_SCREEN_CREATE) {
    if (!hulk::ExtraNativeScreen(ctx, true)) HULK_IMP(HULK_SUB_FE_SCREEN_CREATE)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_SCREEN_FIND));
X86_FUNC(HULK_SUB_FE_SCREEN_FIND) {
    if (!hulk::ExtraNativeScreen(ctx, false)) HULK_IMP(HULK_SUB_FE_SCREEN_FIND)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_SCREEN_FIND_ALT));
X86_FUNC(HULK_SUB_FE_SCREEN_FIND_ALT) {
    if (!hulk::ExtraNativeScreen(ctx, false)) HULK_IMP(HULK_SUB_FE_SCREEN_FIND_ALT)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_CLEAR));
X86_FUNC(HULK_SUB_FE_CLEAR) {
    hulk::ReleaseExtraNativeScreen(ctx);
    HULK_IMP(HULK_SUB_FE_CLEAR)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_DESTROY));
X86_FUNC(HULK_SUB_FE_DESTROY) {
    hulk::ReleaseExtraNativeScreen(ctx);
    HULK_IMP(HULK_SUB_FE_DESTROY)(ctx, base);
}
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_INITIALISE));
X86_FUNC(HULK_SUB_FE_INITIALISE) {
    const uint32_t manager = ctx.ecx;
    HULK_IMP(HULK_SUB_FE_INITIALISE)(ctx, base);
    hulk::InitialiseExtraNativeScreen(ctx, manager);
}

// AudioQualityChanged (thiscall, int; ret 4): only PS2 and GameCube scripts name it, the display options' selectors
// share it with their own owners
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_AUDIO_QUALITY));
X86_FUNC(HULK_SUB_AUDIO_QUALITY) {
    if (hulk::DisplayCallback(ctx.ecx, X86_MEM32(ctx.esp + 4))) {
        ctx.esp += 8;
        return;
    }
    HULK_IMP(HULK_SUB_AUDIO_QUALITY)(ctx, base);
}

// Print (thiscall, argc, argv; ret 8): no Xbox script calls it, the display options screen's sync shares it
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_PRINT));
X86_FUNC(HULK_SUB_PRINT) {
    if (hulk::DisplaySync(ctx, ctx.ecx)) {
        ctx.eax = 0;
        ctx.esp += 12;
        return;
    }
    HULK_IMP(HULK_SUB_PRINT)(ctx, base);
}

// CameraInvertXChanged and CameraInvertYChanged (thiscall, int; ret 4): the player changed one in Camera Options
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_CAMERA_INVERT_X));
X86_FUNC(HULK_SUB_CAMERA_INVERT_X) {
    HULK_IMP(HULK_SUB_CAMERA_INVERT_X)(ctx, base);
    hulk::CameraInvertChanged();
}

X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_CAMERA_INVERT_Y));
X86_FUNC(HULK_SUB_CAMERA_INVERT_Y) {
    HULK_IMP(HULK_SUB_CAMERA_INVERT_Y)(ctx, base);
    hulk::CameraInvertChanged();
}

// the frontend's bottom prompt (thiscall, text, length, label key, button; ret 0x10): appends a button's icon and its
// label, the icons as display_menu.cpp sets them
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_MENU_PROMPT));
X86_FUNC(HULK_SUB_MENU_PROMPT) {
    hulk::MenuButtonPrompts(ctx.ecx);
    HULK_IMP(HULK_SUB_MENU_PROMPT)(ctx, base);
}

// hud_triggerButtonHintBegin (the button's code, the instruction's key): the HUD's mission hints draw that button's
// icon from the hashed name of its code ("\bse" is the A button), a controller's. With the keyboard's prompts the
// code is blanked, and the native text layout resolves its instruction template to the current binding
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_HINT_BEGIN));
X86_FUNC(HULK_SUB_HINT_BEGIN) {
    uint32_t code = 0, original = 0;
    bool patched = false;
    if (!hulk::ControllerPrompts()) {
        // "\bse": the HUD looks the icon up by that name. A space in the escape's place names nothing, so only the
        // instruction's text is drawn, and that carries the key. The scenario's own bytes are put back after the
        // call, which only hashes the name
        code = X86_MEM32(ctx.esp + 4);
        const uint32_t instruction = X86_MEM32(ctx.esp + 8);
        if (code && instruction) hulk::SetHudHintPrompt(
            static_cast<const char*>(kernel::HostPtr(instruction)),
            static_cast<const char*>(kernel::HostPtr(code)));
        if (code) {
            original = X86_MEM32(code);
            if ((original & 0xFF) == 0x08) {
                X86_MEM32(code) = (original & 0xFFFFFF00) | ' ';
                patched = true;
            }
        }
    }
    HULK_IMP(HULK_SUB_HINT_BEGIN)(ctx, base);
    if (patched) X86_MEM32(code) = original;
}

// The game's guarded call (calls a callback that may raise under a VC20 frame; its unwind stub restores esp and
// jumps to the saved return address inside this function, config/game.toml). That address is a continuation, so
// the jump throws X86Resume up the host stack; this frame owns it and resumes there, which unwinds the frames of
// the callback that raised the way the console's longjmp does. The resumed code runs this function's epilogue.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_GUARDED_CALL_A));
X86_FUNC(HULK_SUB_GUARDED_CALL_A) {
    const uint32_t frame = ctx.esp;
    try {
        HULK_IMP(HULK_SUB_GUARDED_CALL_A)(ctx, base);
    } catch (const X86Resume& r) {
        if (r.esp >= frame) throw;
        x86_resume(ctx, base, r);
    }
}

X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_GUARDED_CALL_B));
X86_FUNC(HULK_SUB_GUARDED_CALL_B) {
    const uint32_t frame = ctx.esp;
    try {
        HULK_IMP(HULK_SUB_GUARDED_CALL_B)(ctx, base);
    } catch (const X86Resume& r) {
        if (r.esp >= frame) throw;
        x86_resume(ctx, base, r);
    }
}

// The game's frame swap waits until two vertical blanks have passed since the last swap, which is its 30 fps limit
// in gameplay, or one while loading. Its simulation steps by measured time, so no logic needs that pace: the swap is
// told the blanks have passed and the frame rate is the player's setting, kept by app::Pace after D3DDevice_Swap.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FRAME_SWAP));
X86_FUNC(HULK_SUB_FRAME_SWAP) {
    constexpr uint32_t kBlanksPassed = 2;
    // the world update tells passes of the game loop that showed a frame from those that did not (simulation.h)
    hulk::g_frame_swaps.fetch_add(1, std::memory_order_relaxed);
    X86_MEM32(hulk::addr::kFrameSwapVBlanks) = kBlanksPassed;
    X86_MEM8(hulk::addr::kFrameSwapOneVBlank) = 0;
    hulk::TickNativeControls(ctx);
    HULK_IMP(HULK_SUB_FRAME_SWAP)(ctx, base);
}

// XGetLanguage (language.h): the language settings.toml names, where the runtime's console would always say English
X86_FUNC(HULK_SUB_GET_LANGUAGE) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGI("language: the game is told %s", hulk::LanguageName(hulk::GetLanguage()));
    }
    ctx.eax = uint32_t(hulk::GetLanguage());
    ctx.esp += 4;
}

// the chase camera's update (view.h): its base distance (+0x440, what the scripts set) scaled by Camera Distance for
// the update, which eases the camera's distance toward it, then put back unless the update set a new one
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_CHASE_CAM_UPDATE));
X86_FUNC(HULK_SUB_CHASE_CAM_UPDATE) {
    constexpr uint32_t kBaseDistance = 0x440;
    const uint32_t camera = ctx.ecx;
    // the wheel's zoom glides with real time, whatever the frame rate
    static auto last = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    hulk::TickCameraDistance(std::chrono::duration<float>(now - last).count());
    last = now;
    const uint32_t own = X86_MEM32(camera + kBaseDistance);
    const uint32_t scaled = std::bit_cast<uint32_t>(hulk::AdjustCameraDistance(std::bit_cast<float>(own)));
    X86_MEM32(camera + kBaseDistance) = scaled;
    HULK_IMP(HULK_SUB_CHASE_CAM_UPDATE)(ctx, base);
    if (X86_MEM32(camera + kBaseDistance) == scaled) X86_MEM32(camera + kBaseDistance) = own;
}

// The bloom pass draws screen quads whose stages read their own texture coordinates, and never sets that: it trusts
// what the scene left. A scene object can leave stage 1 reading set 0, and the game's cached setter then keeps it, so
// the pass samples the 640x480 scene with the bloom texture's 160x120 coordinates: the top left quarter blown up under
// a bloom of the right size, in flashes (issue #2). The pass gets the coordinates it means; the scene's values go back
// after it, since that cache still believes them.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_POST_EFFECT));
X86_FUNC(HULK_SUB_POST_EFFECT) {
    constexpr uint32_t kStages = 4, kStageStates = 0x80, kTexCoordIndex = 0x1C * 4;
    const auto state = [](uint32_t stage) { return hulk::addr::kTextureStates + stage * kStageStates + kTexCoordIndex; };
    uint32_t scene[kStages];
    for (uint32_t s = 0; s < kStages; s++) {
        scene[s] = X86_MEM32(state(s));
        if (scene[s] != s) kernel::CallGuest(ctx, hulk::addr::kSetTexCoordIndex, {s, s});
    }
    HULK_IMP(HULK_SUB_POST_EFFECT)(ctx, base);
    const uint32_t result = ctx.eax;
    for (uint32_t s = 0; s < kStages; s++)
        if (scene[s] != s) kernel::CallGuest(ctx, hulk::addr::kSetTexCoordIndex, {s, scene[s]});
    ctx.eax = result;
}

// Field of view and render distance (view.h). The render manager keeps the camera the game gives it (fov at +0x5C,
// aspect, near, far at +0x68) and hands it to its render layer when the byte at +0x6C is clear, which the game does
// whenever the camera changes. A changed setting clears it too, so a still camera (a paused game) shows it at once.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_RENDER_FRAME));
X86_FUNC(HULK_SUB_RENDER_FRAME) {
    constexpr uint32_t kViewSet = 0x6C;
    static uint32_t applied = 0;
    if (const uint32_t generation = hulk::ViewGeneration(); generation != applied) {
        const uint32_t manager = X86_MEM32(hulk::addr::kRenderManager);
        if (manager && X86_MEM32(manager)) {
            X86_MEM8(manager + kViewSet) = 0;
            applied = generation;
        }
    }
    HULK_IMP(HULK_SUB_RENDER_FRAME)(ctx, base);
}

// The render layer's camera (thiscall: transform, fov, aspect, near, far; ret 0x14). The layer gives them to its
// Pure3D camera, which builds the projection and frustum from them, and works out its own culling angles from the
// same fov, so widening the field here widens what is drawn and what is kept. The far plane moves out with the render
// distance. The arguments are the caller's copies; the render manager keeps the game's own values.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_SET_VIEW));
X86_FUNC(HULK_SUB_SET_VIEW) {
    constexpr uint32_t kFov = 8, kFar = 0x14;
    const float fov = std::bit_cast<float>(uint32_t(X86_MEM32(ctx.esp + kFov)));
    const float far_plane = std::bit_cast<float>(uint32_t(X86_MEM32(ctx.esp + kFar)));
    const float wide = hulk::AdjustFov(fov), distant = hulk::AdjustDistance(far_plane);
    static uint32_t logged = ~0u;
    if (const uint32_t generation = hulk::ViewGeneration(); generation != logged) {
        logged = generation;
        LOGI("view: fov %.1f -> %.1f degrees, far plane %.0f -> %.0f", fov * 57.29578f, wide * 57.29578f, far_plane, distant);
    }
    X86_MEM32(ctx.esp + kFov) = std::bit_cast<uint32_t>(wide);
    X86_MEM32(ctx.esp + kFar) = std::bit_cast<uint32_t>(distant);
    HULK_IMP(HULK_SUB_SET_VIEW)(ctx, base);
}

// The render layer's frame setup (thiscall): gives the fog planes the scripts set (rm_SetFogPlanes: +0xE4, +0xE8) to
// the frame. They move out with the far plane for the call and are put back after it, so the scripts' values stay.
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_APPLY_FOG));
X86_FUNC(HULK_SUB_APPLY_FOG) {
    constexpr uint32_t kFogNear = 0xE4, kFogFar = 0xE8;
    const uint32_t layer = ctx.ecx;
    constexpr uint32_t kFogOn = 0xD9;  // rm_EnableFog
    const uint32_t fog_near = X86_MEM32(layer + kFogNear), fog_far = X86_MEM32(layer + kFogFar);
    // the scripts' fog as it changes, and the setting's at each change of it
    static uint32_t seen_near = ~0u, seen_far = ~0u, seen_generation = ~0u;
    static uint8_t seen_on = 0xFF;
    const uint8_t on = X86_MEM8(layer + kFogOn);
    if (fog_near != seen_near || fog_far != seen_far || on != seen_on || hulk::ViewGeneration() != seen_generation) {
        seen_near = fog_near, seen_far = fog_far, seen_on = on, seen_generation = hulk::ViewGeneration();
        const float n = std::bit_cast<float>(fog_near), f = std::bit_cast<float>(fog_far);
        LOGI("view: fog %s, planes %.0f-%.0f -> %.0f-%.0f", on ? "on" : "off", n, f, hulk::AdjustDistance(n),
             hulk::AdjustDistance(f));
    }
    X86_MEM32(layer + kFogNear) = std::bit_cast<uint32_t>(hulk::AdjustDistance(std::bit_cast<float>(fog_near)));
    X86_MEM32(layer + kFogFar) = std::bit_cast<uint32_t>(hulk::AdjustDistance(std::bit_cast<float>(fog_far)));
    HULK_IMP(HULK_SUB_APPLY_FOG)(ctx, base);
    X86_MEM32(layer + kFogNear) = fog_near;
    X86_MEM32(layer + kFogFar) = fog_far;
}
