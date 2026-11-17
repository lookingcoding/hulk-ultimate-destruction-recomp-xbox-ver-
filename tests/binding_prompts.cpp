#include "common/binding_prompts.h"
#include "common/native_hints.h"
#include "common/addresses.h"
#include "kernel/kernel.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

namespace {
std::vector<uint8_t> memory(0x200000);
uint32_t next = 0x100000;
int allocations = 0, frees = 0, failures = 0;
void Check(bool ok, const char* why) { if (!ok) { ++failures; std::printf("FAIL: %s\n", why); } }
void Text(uint32_t at, std::u16string_view text) {
    std::memcpy(memory.data() + at, text.data(), text.size() * 2);
    std::memset(memory.data() + at + text.size() * 2, 0, 2);
}
std::u16string Read(uint32_t at) { return reinterpret_cast<const char16_t*>(memory.data() + at); }
}
namespace kernel {
uint8_t* g_base = memory.data();
uint32_t CallGuest(X86Context&, uint32_t fn, const uint32_t* args, size_t count) {
    if (fn == hulk::addr::kGuestNew && count == 1) {
        ++allocations; uint32_t p = next; next += (args[0] + 15) & ~15u; return p;
    }
    #ifdef HULK_USA
    constexpr uint32_t free_address = 0x8D340;
#else
    constexpr uint32_t free_address = 0x8D3E0;
#endif
    if (fn == free_address && count == 1) { ++frees; return 0; }
    Check(false, "unexpected guest operation"); return 0;
}
}
int main() {
    using namespace hulk;
    using bindings::Action;
    auto map = bindings::Defaults();
    for (size_t i = 0; i < bindings::ActionCount; ++i) map.inputs[i] = {'K', 'L'};
    bindings::Set(map);
    for (int language = 0; language < 4; ++language) {
        for (size_t i = 0; i < bindings::ActionCount; ++i) {
            auto token = BindingPromptTemplate("{pc:" + std::string(bindings::Info(Action(i)).key) + "}", language);
            Check(ExpandBindingPrompts(std::u16string(token.begin(), token.end())) == u"[K / L]", "every action uses primary and alternate in every language");
        }
    }
    Check(ExpandBindingPrompts(u"Hold {pc0:hold_jump} to jump") == u"Hold [Hold K / L] to jump", "charged actions use the same live bindings");
    Check(ExpandBindingPrompts(u"{pc1:hold_jump}") == u"[Maintenir K / L]", "localized hold wording retained");
    Check(ExpandBindingPrompts(u"{pc0:move}") == u"[K / L, K / L, K / L, K / L]", "movement hint follows all four movement bindings");
    Check(ExpandBindingPrompts(u"{pc0:around}") == u"[K / L, K / L]", "left/right hint follows movement bindings");
    Check(ExpandBindingPrompts(u"plain text \bmh {pc9:jump} {pc0:unknown}") == u"plain text \bmh {pc9:jump} {pc0:unknown}", "ordinary text, map icons and unknown tokens remain unchanged");
    map.inputs[size_t(Action::Jump)].clear(); bindings::Set(map);
    Check(ExpandBindingPrompts(u"{pc0:jump}") == u"[Unbound]", "unbound action does not advertise a default key");
    bindings::Reset();
    const std::u16string cached_hint = u"{pc0:jump} Jump to City";
    Check(ExpandBindingPrompts(cached_hint) == u"[Space / Enter] Jump to City", "default hint shows both jump keys");
    map = bindings::Defaults(); map.inputs[size_t(Action::Jump)] = {'J', 'K'}; bindings::Set(map);
    Check(ExpandBindingPrompts(cached_hint) == u"[J / K] Jump to City", "already-cached hint updates without loading strings again");
    Check(BindingPrompt(Action::Jump, 0, false) == u"[J]", "compact prompt uses primary key");

    SetHudHintPrompt("TBS_INSTRUCTION_DROP", "\bus");
    const auto hud_hint = u"{pc0:hint:TBS_INSTRUCTION_DROP} Drop Item";
    map = bindings::Defaults(); map.inputs[size_t(Action::Grab)] = {'G', 'H'}; bindings::Set(map);
    Check(ExpandBindingPrompts(hud_hint) == u"[G / H] Drop Item", "HUD hint uses the actual scenario button and current keys");
    bindings::Reset();
    Check(ExpandBindingPrompts(hud_hint) == u"[Q] Drop Item", "active HUD hint updates immediately on reset");
    Check(ExpandBindingPrompts(u"{pc0:hint:unused} No button") == u" No button", "unused instruction does not invent a default key");
    map = bindings::Defaults(); map.inputs[size_t(Action::Jump)] = {'J', 'K'}; bindings::Set(map);

    // The same cached footer is drawn before rebind, after rebind and after reset; no menu rebuild.
    Text(0x6000, u"{pc0:mb} Back");
    X86Context draw{}; draw.esp = 0x90000; draw.ecx = 0x7000;
    for (int i = 0; i < 7; ++i) kernel::W32(draw.esp + i * 4, 0x12340000 + i);
    kernel::W32(draw.esp + 4, 0x6000);
    const auto check_draw = [&](const char16_t* expected) {
        const auto old = draw.esp;
        const auto shift = PrepareNativePromptDraw(draw);
        Check(Read(kernel::R32(draw.esp + 4)) == expected, "cached native footer resolves the latest primary key");
        Check(kernel::R32(draw.esp) == 0x12340000 && kernel::R32(draw.esp + 24) == 0x12340006,
              "native draw retains return address and all other arguments");
        Check(kernel::R32(draw.esp + 4) > draw.esp + 28, "native draw stack cannot overwrite temporary text");
        draw.esp += 28; draw.esp += shift;
        Check(draw.esp == old + 28 && draw.ecx == 0x7000, "native draw return stack and this pointer preserved");
        draw.esp = old;
    };
    check_draw(u"[Q] Back");
    auto footer_map = *bindings::Get(); footer_map.inputs[size_t(Action::MenuBack)] = {'K', 'L'};
    bindings::Set(footer_map); check_draw(u"[K] Back");
    bindings::Reset(); check_draw(u"[Q] Back");
    map = bindings::Defaults(); map.inputs[size_t(Action::Jump)] = {'J', 'K'}; bindings::Set(map);

    // A real FEString keeps the original template and a separately owned native layout buffer.
    X86Context ctx{}; ctx.esp = 0x80000; ctx.ecx = 0x2000;
    kernel::W32(ctx.esp + 4, 0x3000);
    Text(0x4000, cached_hint); kernel::W32(ctx.ecx + 0x10, 0x4000);
    kernel::W32(ctx.ecx + 0x20, 123);
    PrepareNativeHint(ctx);
    const uint32_t first = kernel::R32(ctx.ecx + 0x10);
    Check(Read(first) == u"[J / K] Jump to City", "native layout receives the bound keys before wrapping");
    Check(kernel::R32(ctx.ecx + 0x20) == 0xFFFFFFFF, "binding text invalidates native wrapped-text cache");
    Check(ctx.esp == 0x80000 && ctx.ecx == 0x2000, "native preparation preserves guest registers and stack");
    kernel::W32(ctx.ecx + 0x10, 0x4000); // hook restores the original pointer after native layout
    PrepareNativeHint(ctx);
    Check(kernel::R32(ctx.ecx + 0x10) == first && allocations == 1, "unchanged live hint reuses its owned buffer");
    kernel::W32(ctx.ecx + 0x10, 0x4000);
    bindings::Reset(); PrepareNativeHint(ctx);
    Check(Read(kernel::R32(ctx.ecx + 0x10)) == u"[Space / Enter] Jump to City", "native cached hint updates immediately after reset");
    Check(allocations == 2 && frees == 1, "rebinding replaces and frees old native text");
    kernel::W32(ctx.ecx + 0x10, 0x4000);
    ReleaseNativeHint(ctx); ReleaseNativeHint(ctx);
    Check(frees == 2, "native text destruction frees buffer once");
    // Keyed strings follow the same path, without converting the original FEString to a raw string.
    kernel::W32(ctx.ecx + 0x10, 0); kernel::W32(ctx.ecx + 0x14, 2);
    kernel::W32(0x3000 + 0x10, 2); kernel::W32(0x3000 + 0x0C, 0x5000); kernel::W32(0x5000 + 8, 0x4000);
    PrepareNativeHint(ctx);
    Check(Read(kernel::R32(ctx.ecx + 0x10)) == u"[Space / Enter] Jump to City", "translated table entry resolves live bindings");
    kernel::W32(ctx.ecx + 0x10, 0); ReleaseNativeHint(ctx);
    Check(kernel::R32(ctx.ecx + 0x14) == 2 && allocations == frees, "translated source and native ownership survive cleanup");
    return failures ? 1 : 0;
}
