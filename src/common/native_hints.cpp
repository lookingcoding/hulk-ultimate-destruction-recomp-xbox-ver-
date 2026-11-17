#include "common/native_hints.h"
#include "common/binding_prompts.h"
#include "common/addresses.h"
#include "kernel/kernel.h"
#include <cstring>
#include <unordered_map>

namespace hulk {
namespace {
struct Text { uint32_t buffer = 0; std::u16string rendered; };
// FEString layout/destruction runs on the guest frontend thread.
std::unordered_map<uint32_t, Text> texts;
#ifdef HULK_USA
constexpr uint32_t kDelete = 0x8D340;
#else
constexpr uint32_t kDelete = 0x8D3E0;
#endif
}
void PrepareNativeHint(X86Context& ctx) {
    const auto saved = ctx;
    const uint32_t owner = ctx.ecx;
    uint32_t source = kernel::R32(owner + 0x10);
    if (!source) {
        const uint32_t manager = kernel::R32(ctx.esp + 4);
        const int32_t index = int32_t(kernel::R32(owner + 0x14));
        if (index < 0 || index > int32_t(kernel::R32(manager + 0x10))) return;
        source = kernel::R32(kernel::R32(manager + 0x0C) + uint32_t(index) * 4);
    }
    if (!source) return;
    const auto* chars = static_cast<const char16_t*>(kernel::HostPtr(source));
    size_t length = 0;
    while (length < 4095 && chars[length]) ++length;
    const std::u16string_view original(chars, length);
    if (original.find(u"{pc") == std::u16string_view::npos) return;
    const auto rendered = ExpandBindingPrompts(original);
    auto& cached = texts[owner];
    if (cached.rendered != rendered || !cached.buffer) {
        const uint32_t bytes = uint32_t((rendered.size() + 1) * 2);
        const uint32_t buffer = kernel::CallGuest(ctx, addr::kGuestNew, {bytes});
        if (!buffer) { ctx = saved; return; }
        std::memcpy(kernel::HostPtr(buffer), rendered.c_str(), bytes);
        if (cached.buffer) kernel::CallGuest(ctx, kDelete, {cached.buffer});
        cached = {buffer, rendered};
        kernel::W32(owner + 0x20, 0xFFFFFFFF); // let the game recalculate wrapping for the new keys
    }
    kernel::W32(owner + 0x10, cached.buffer);
    ctx = saved;
}
void ReleaseNativeHint(X86Context& ctx) {
    auto found = texts.find(ctx.ecx);
    if (found == texts.end()) return;
    const auto saved = ctx;
    const uint32_t buffer = found->second.buffer;
    texts.erase(found);
    if (buffer) kernel::CallGuest(ctx, kDelete, {buffer});
    ctx = saved;
}
uint32_t PrepareNativePromptDraw(X86Context& ctx) {
    const uint32_t source = kernel::R32(ctx.esp + 4);
    if (!source) return 0;
    const auto* chars = static_cast<const char16_t*>(kernel::HostPtr(source));
    size_t length = 0;
    while (length < 4095 && chars[length]) ++length;
    const std::u16string_view text(chars, length);
    if (text.find(u"{pc") == std::u16string_view::npos) return 0;
    const auto rendered = ExpandBindingPrompts(text);
    const uint32_t old = ctx.esp;
    const uint32_t bytes = uint32_t((rendered.size() + 1) * 2);
    const uint32_t buffer = (old - bytes) & ~15u;
    // Keep the buffer ABOVE the temporary call frame: the native renderer grows its stack below it.
    const uint32_t frame = buffer - 32;
    std::memcpy(kernel::HostPtr(buffer), rendered.c_str(), bytes);
    std::memcpy(kernel::HostPtr(frame), kernel::HostPtr(old), 28); // return address and six thiscall args
    kernel::W32(frame + 4, buffer);
    ctx.esp = frame;
    return old - frame;
}
}
