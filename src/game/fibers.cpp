// XDK 5849 fibers in this XBE: CreateFiber 00350E1E, SwitchToFiber
// 00350EBD, DeleteFiber 00350EAA, ConvertThreadToFiber 00350F03.
// Switching the guest ESP alone cannot suspend the recompiled C++ call stack.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <memory>
#include <unordered_map>
#include "xdk/hle.h"

using namespace kernel;
namespace {
constexpr uint32_t kSwitch = 0x00350EBD;
constexpr uint32_t kBootstrap = 0x00350DD9;
struct Fiber {
    void* native = nullptr;
    X86Context* context = nullptr;
    uint8_t* base = nullptr;
    uint32_t entry = 0;
};
// The guest TLS and every native fiber here belong to one kernel thread.
thread_local std::unordered_map<uint32_t, std::unique_ptr<Fiber>> fibers;

void WINAPI StartFiber(void* parameter) {
    auto& fiber = *static_cast<Fiber*>(parameter);
    x86_icall(*fiber.context, fiber.base, fiber.entry);
    x86_trap(*fiber.context, kSwitch, "Xbox fiber entry returned unexpectedly");
}
}

X86_FUNC(sub_00350EBD) {
    const uint32_t next = R32(ctx.esp + 4);
    const uint32_t stackBase = R32(ctx.fs_base + 4);
    const uint32_t tlsIndex = R32(0x004E7BDC);
    const uint32_t tls = R32(stackBase + tlsIndex * 4);
    const uint32_t current = R32(tls + 0x10);
    if (!next || !current) x86_trap(ctx, kSwitch, "invalid Xbox fiber record");

    if (!fibers.count(current)) {
        auto root = std::make_unique<Fiber>();
        root->native = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH);
        if (!root->native) x86_trap(ctx, kSwitch, "could not convert host thread to fiber");
        root->context = &ctx;
        root->base = base;
        fibers.emplace(current, std::move(root));
    }

    // Reproduce the guest save/restore sequence, including the SEH chain and
    // stdcall ret 4. The native switch below preserves the C++ continuation.
    ctx.eax = next;
    ctx.edx = tls;
    ctx.ecx = current;
    X86_PUSH32(ctx.ebp);
    X86_PUSH32(ctx.esi);
    X86_PUSH32(ctx.edi);
    X86_PUSH32(ctx.ebx);
    X86_PUSH32(R32(ctx.fs_base));
    W32(current + 0x0C, ctx.esp);
    ctx.esp = R32(next + 0x0C);
    W32(tls + 0x10, next);
    W32(ctx.fs_base, R32(ctx.esp)); ctx.esp += 4;
    ctx.ebx = R32(ctx.esp); ctx.esp += 4;
    ctx.edi = R32(ctx.esp); ctx.esp += 4;
    ctx.esi = R32(ctx.esp); ctx.esp += 4;
    ctx.ebp = R32(ctx.esp); ctx.esp += 4;
    const uint32_t entry = R32(ctx.esp);
    ctx.esp += 8;
    if (next == current) return;

    if (!fibers.count(next)) {
        // A new fiber starts at the XDK bootstrap. A different continuation
        // here would require a missing/migrated native stack; fail explicitly.
        if (entry != kBootstrap) x86_trap(ctx, kSwitch, "unknown resumed or migrated Xbox fiber");
        auto fiber = std::make_unique<Fiber>();
        fiber->context = &ctx;
        fiber->base = base;
        fiber->entry = entry;
        fiber->native = CreateFiberEx(64 * 1024, 64 * 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, StartFiber, fiber.get());
        if (!fiber->native) x86_trap(ctx, kSwitch, "could not allocate host fiber");
        LOGI("fiber: created native stack for guest %08X", next);
        fibers.emplace(next, std::move(fiber));
    }
    SwitchToFiber(fibers.at(next)->native);
}

X86_EXTERN_FUNC(__imp__sub_00350EAA);
X86_FUNC(sub_00350EAA) {
    const uint32_t guest = R32(ctx.esp + 4);
    if (auto it = fibers.find(guest); it != fibers.end()) {
        if (it->second->native == GetCurrentFiber()) x86_trap(ctx, 0x00350EAA, "deleting the active Xbox fiber");
        DeleteFiber(it->second->native);
        fibers.erase(it);
    }
    __imp__sub_00350EAA(ctx, base);
}
