#include "common/native_screen.h"
#include "kernel/kernel.h"
#include <cstring>
#include <unordered_map>
namespace hulk {
namespace {
// Frontend creation, lookup and destruction run on the guest frontend thread.
std::unordered_map<uint32_t, uint32_t> screens;
}
bool ExtraNativeScreen(X86Context& ctx, bool create) {
    auto* name = static_cast<const char*>(kernel::HostPtr(kernel::R32(ctx.esp + 4)));
    if (!name || std::strcmp(name, "pc_keybinds") != 0) return false;
    const auto saved = ctx;
    auto found = screens.find(saved.ecx);
    uint32_t screen = found == screens.end() ? 0 : found->second;
    if (!screen && create) {
        // FEThings::GetScreen uses this exact factory (vtable +0xC8).
        screen = kernel::CallGuest(ctx, kernel::R32(kernel::R32(saved.ecx) + 0xC8),
                                   {saved.ecx, kernel::R32(saved.esp + 4)});
        if (screen) screens.emplace(saved.ecx, screen);
    }
    ctx = saved;
    ctx.eax = screen;
    ctx.esp += 8; // thiscall, one argument, ret 4
    return true;
}
void InitialiseExtraNativeScreen(X86Context& ctx, uint32_t manager) {
    auto found = screens.find(manager);
    if (found == screens.end()) return;
    const auto saved = ctx;
    ctx.ecx = found->second;
    // FEThings::Initialise calls +0x24 on every screen to bind its native locators.
    kernel::CallGuest(ctx, kernel::R32(kernel::R32(found->second) + 0x24), {});
    ctx = saved;
}
void ReleaseExtraNativeScreen(X86Context& ctx) {
    auto found = screens.find(ctx.ecx);
    if (found == screens.end()) return;
    const auto saved = ctx;
    const auto screen = found->second;
    screens.erase(found); // clear ownership before the native destructor can reenter
    for (uint32_t offset : {0x175Cu, 0x1768u})
        if (kernel::R32(saved.ecx + offset) == screen) kernel::W32(saved.ecx + offset, 0);
    ctx.ecx = screen;
    // Same deleting destructor used by FEThingsBase's screen cleanup loop.
    kernel::CallGuest(ctx, kernel::R32(kernel::R32(screen)), {1});
    ctx = saved;
}
}
