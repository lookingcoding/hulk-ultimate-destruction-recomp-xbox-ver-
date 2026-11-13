#include <bit>
#include <cmath>
#include <cstdlib>
#include "common/addresses.h"
#include "common/cheats.h"
#include "common/cheat_movement_addresses.h"
#include "core/log.h"
#include "xdk/hle.h"

namespace {
uint32_t FindCharacter(uint32_t action, uint8_t* base) {
    const uint32_t owner = X86_MEM32(action + 0x1c);
    const uint32_t app = X86_MEM32(hulk::addr::kApp);
    const uint32_t manager = app ? uint32_t(X86_MEM32(app + 0x44)) : 0;
    if (!owner || !manager) return 0;
    const uint32_t count = X86_MEM32(manager + 4);
    const uint32_t entries = X86_MEM32(manager + 8), active = X86_MEM32(manager + 12);
    const uint32_t id = X86_MEM32(owner + 8);
    if (count >= 65536 || !entries || !active) return 0;
    for (uint32_t i = 0; i < count; ++i)
        if (X86_MEM8(active + i) && uint32_t(X86_MEM32(entries + i * 0x24)) == id)
            return X86_MEM32(entries + i * 0x24 + 0x10);
    return 0;
}
}
// Opt-in diagnostics for the retail CJumpAction start routine. This hook does
// not change guest state; it identifies the actor and jump parameters.
X86_EXTERN_FUNC(__imp__sub_00013B90);
X86_FUNC(HULK_JUMP_START) {
    const uint32_t action = ctx.ecx;
    __imp__sub_00013B90(ctx, base);
    if (std::getenv("HULK_TEST_MOVEMENT_AUDIT")) {
        const uint32_t character = FindCharacter(action, base);
        LOGI("movement audit: jump action=%08x owner=%08x track=%08x active=%08x v=%.4f h=%.4f g=%.4f",
             action, uint32_t(X86_MEM32(action + 0x1c)), uint32_t(X86_MEM32(action + 0x20)),
             uint32_t(X86_MEM32(action + 0x24)), std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x28))),
             std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x2c))),
             std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x30))));
        const uint32_t app = X86_MEM32(hulk::addr::kApp);
        const uint32_t actors = app ? uint32_t(X86_MEM32(app + 0x20)) : 0;
        const uint32_t player = actors ? uint32_t(X86_MEM32(actors + 0x124)) : 0;
        if (character) LOGI("movement audit: character=%08x player=%08x namehash=%08x:%08x", character, player,
                            uint32_t(X86_MEM32(character + 0xc)), uint32_t(X86_MEM32(character + 8)));
    }
}
X86_EXTERN_FUNC(__imp__sub_00018D40);
X86_FUNC(HULK_JUMP_UPDATE) {
    const uint32_t action = ctx.ecx;
    const bool audit = std::getenv("HULK_TEST_MOVEMENT_AUDIT") != nullptr;
    const bool enabled = hulk::cheats::Level(hulk::cheats::Cheat::SuperJump) != 0;
    if (!audit && !enabled) { __imp__sub_00018D40(ctx, base); return; }
    uint32_t character = 0;
    if (audit || enabled) character = FindCharacter(action, base);
    const uint32_t app = (enabled && character) ? uint32_t(X86_MEM32(hulk::addr::kApp)) : 0;
    const uint32_t actors = app ? uint32_t(X86_MEM32(app + 0x20)) : 0;
    const uint32_t player = actors ? uint32_t(X86_MEM32(actors + 0x124)) : 0;
    const float velocity = std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x28)));
    const float gravity = std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x30)));
    constexpr float factor = 3.0f;
    const bool scale = enabled && player && character == player && std::isfinite(velocity) &&
                       std::isfinite(gravity) && std::abs(velocity) < 10000 && std::abs(gravity) < 10000;
    if (scale) {
        X86_MEM32(action + 0x28) = std::bit_cast<uint32_t>(velocity * factor);
        X86_MEM32(action + 0x30) = std::bit_cast<uint32_t>(gravity * factor);
    }
    __imp__sub_00018D40(ctx, base);
    // The action keeps its ordinary velocity/acceleration between calls. Scaling
    // both only during this update triples vertical displacement while preserving
    // jump duration. Switching off in flight therefore resumes the normal update.
    if (scale) {
        const float after = std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x28)));
        X86_MEM32(action + 0x28) = std::bit_cast<uint32_t>(after / factor);
        X86_MEM32(action + 0x30) = std::bit_cast<uint32_t>(gravity);
    }
    if (!audit) return;
    if (!character) return;
    const uint32_t spatial = X86_MEM32(character + 0x10);
    if (!spatial) return;
    const uint32_t matrix = X86_MEM32(spatial + 0x18);
    const uint32_t y = matrix ? matrix + 0x44 : spatial + 8;
    static unsigned calls = 0;
    if (++calls % 6 == 0) LOGI("movement audit: sample character=%08x y=%.4f v=%.4f g=%.4f super=%u", character,
        std::bit_cast<float>(uint32_t(X86_MEM32(y))), std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x28))),
        std::bit_cast<float>(uint32_t(X86_MEM32(action + 0x30))), unsigned(scale));
}
