// Hooks for default.xbe. A strong definition replaces the recompiled function at that address, for example:
//
// #include "xdk/hle.h"
//
// X86_EXTERN_FUNC(__imp__sub_00012345);
// X86_FUNC(sub_00012345) {
//     __imp__sub_00012345(ctx, base);  // the original
// }
#include "common/display_menu.h"
#include "xdk/hle.h"

// the frontend's callbacks object (thiscall, owner; ret 4): registers each function the FE scripts name, then the
// display options' selectors (display_menu.cpp)
X86_EXTERN_FUNC(__imp__sub_000C4500);
X86_FUNC(sub_000C4500) {
    __imp__sub_000C4500(ctx, base);
    hulk::RegisterDisplayCallbacks(ctx);
}

// AudioQualityChanged (thiscall, int; ret 4): only PS2 and GameCube scripts name it, the display options' selectors
// share it with their own owners
X86_EXTERN_FUNC(__imp__sub_000BBEC0);
X86_FUNC(sub_000BBEC0) {
    if (hulk::DisplayCallback(ctx.ecx, X86_MEM32(ctx.esp + 4))) {
        ctx.esp += 8;
        return;
    }
    __imp__sub_000BBEC0(ctx, base);
}

// Print (thiscall, argc, argv; ret 8): no Xbox script calls it, the display options screen's sync shares it
X86_EXTERN_FUNC(__imp__sub_000B46B0);
X86_FUNC(sub_000B46B0) {
    if (hulk::DisplaySync(ctx, ctx.ecx)) {
        ctx.eax = 0;
        ctx.esp += 12;
        return;
    }
    __imp__sub_000B46B0(ctx, base);
}

// the frontend's bottom prompt (thiscall, text, length, label key, button; ret 0x10): appends a button's icon and its
// label, the icons as display_menu.cpp sets them
X86_EXTERN_FUNC(__imp__sub_000BCA60);
X86_FUNC(sub_000BCA60) {
    hulk::MenuButtonPrompts(ctx.ecx);
    __imp__sub_000BCA60(ctx, base);
}
