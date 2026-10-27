// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include "common/addresses.h"
#include "common/camera.h"
#include "common/display_menu.h"
#include "xdk/hle.h"

// the frontend's callbacks object (thiscall, owner; ret 4): registers each function the FE scripts name, then the
// display options' selectors (display_menu.cpp)
X86_EXTERN_FUNC(HULK_IMP(HULK_SUB_FE_CALLBACKS));
X86_FUNC(HULK_SUB_FE_CALLBACKS) {
    HULK_IMP(HULK_SUB_FE_CALLBACKS)(ctx, base);
    hulk::RegisterDisplayCallbacks(ctx);
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
