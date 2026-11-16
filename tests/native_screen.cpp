#include "common/native_screen.h"
#include "kernel/kernel.h"
#include <cstdio>
#include <cstring>
#include <vector>
namespace {
std::vector<uint8_t> memory(0x600000);
unsigned created = 0, initialised = 0, deleted = 0, failures = 0;
void Check(bool ok, const char* text) { if (!ok) { std::printf("FAIL: %s\n", text); failures++; } }
}
namespace kernel {
uint8_t* g_base = memory.data();
uint32_t CallGuest(X86Context& ctx, uint32_t function, const uint32_t* args, size_t count) {
    if (function == 0x900001) {
        Check(count == 2 && args[0] == ctx.ecx, "native factory ABI");
        Check(std::strcmp(static_cast<const char*>(HostPtr(args[1])), "pc_keybinds") == 0, "native page name");
        uint32_t screen = 0x30000 + (++created) * 0x1000;
        W32(screen, 0x28000); W32(0x28000, 0x900002); W32(0x28024, 0x900003);
        return screen;
    }
    if (function == 0x900002) { Check(count == 1 && args[0] == 1, "native deleting destructor ABI"); deleted++; return 0; }
    if (function == 0x900003) { Check(count == 0, "native locator initialisation ABI"); initialised++; return 0; }
    failures++; return 0;
}
}
int main() {
    X86Context ctx{}; ctx.esp = 0x10000; ctx.ecx = 0x20000;
    kernel::W32(ctx.ecx, 0x27000); kernel::W32(0x270C8, 0x900001);
    for (unsigned i = 0; i < 64; i++) kernel::W32(ctx.ecx + 0xC + i * 4, 0x40000 + i * 4);
    std::memcpy(kernel::HostPtr(0x18000), "pc_keybinds", 12);
    kernel::W32(ctx.esp + 4, 0x18000);
    Check(hulk::ExtraNativeScreen(ctx, true), "extra page uses native factory beyond full cache");
    Check(created == 1 && ctx.eax == 0x31000 && ctx.esp == 0x10008 && ctx.ecx == 0x20000, "lookup return ABI");
    hulk::InitialiseExtraNativeScreen(ctx, 0x20000);
    Check(initialised == 1, "native page locators are initialised alongside cached screens");
    for (unsigned i = 0; i < 64; i++) Check(kernel::R32(0x2000C + i * 4) == 0x40000 + i * 4, "retail screen slots unchanged");
    ctx.esp = 0x10000;
    Check(hulk::ExtraNativeScreen(ctx, false) && ctx.eax == 0x31000 && created == 1, "navigation finds the same page");
    ctx.esp = 0x10000;
    Check(hulk::ExtraNativeScreen(ctx, true) && created == 1, "repeated script lookup never duplicates ownership");
    kernel::W32(0x2175C, 0x31000); kernel::W32(0x21768, 0x31000);
    auto saved = ctx;
    hulk::ReleaseExtraNativeScreen(ctx);
    Check(deleted == 1 && kernel::R32(0x2175C) == 0 && kernel::R32(0x21768) == 0, "reset destroys page and clears active references");
    Check(ctx.ecx == saved.ecx && ctx.esp == saved.esp, "cleanup preserves guest context");
    hulk::ReleaseExtraNativeScreen(ctx); Check(deleted == 1, "clear then destructor cannot double-delete");
    ctx.esp = 0x10000; hulk::ExtraNativeScreen(ctx, true);
    Check(created == 2 && ctx.eax == 0x32000, "frontend reload receives a fresh native page");
    hulk::ReleaseExtraNativeScreen(ctx);
    std::memcpy(kernel::HostPtr(0x18000), "options", 8); ctx.esp = 0x10000;
    Check(!hulk::ExtraNativeScreen(ctx, true) && ctx.esp == 0x10000, "original menus retain original lookup");
    return failures ? 1 : 0;
}
