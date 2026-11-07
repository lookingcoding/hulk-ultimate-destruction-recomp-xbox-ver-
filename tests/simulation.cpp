#include <cstdio>
#include <bit>
#include <limits>
#include <stdexcept>
#include <vector>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "common/simulation.h"
#include "common/simulation_context.h"
#include "common/addresses.h"
#include "cpu/x86_ops.h"
#include "core/log.h"
namespace logx { void Write(Level, const char*, ...) {} }

#ifdef HULK_USA
#define WORLD sub_00088C30
#define ORIGINAL_WORLD __imp__sub_00088C30
#define ELAPSED sub_000860C0
#define ORIGINAL_ELAPSED __imp__sub_000860C0
#define UNSCALED sub_00085F80
#define ORIGINAL_UNSCALED __imp__sub_00085F80
#else
#define WORLD sub_00088CD0
#define ORIGINAL_WORLD __imp__sub_00088CD0
#define ELAPSED sub_000860A0
#define ORIGINAL_ELAPSED __imp__sub_000860A0
#define UNSCALED sub_00085F60
#define ORIGINAL_UNSCALED __imp__sub_00085F60
#endif
X86_EXTERN_FUNC(WORLD);
X86_EXTERN_FUNC(ELAPSED);
X86_EXTERN_FUNC(UNSCALED);
namespace {
unsigned world_calls = 0, raw_clock_calls = 0;
bool bad_tick = false, throw_tick = false;
constexpr float raw_delta = 1.0f / 144;
float wall_delta = raw_delta, expected_delta = hulk::SimulationClock::kStep;
constexpr uint32_t main_clock = 0x200, alternate_clock = 0x300;
float alternate_delta = 0;
unsigned alternate_reads = 0;
struct FiberProbe {
    void* root = nullptr;
    void* child = nullptr;
    bool converted = false, entered = false, child_raw = false, resumed_raw = false, parent_fixed = false;
    X86Context snapshot{};
    uint8_t* base = nullptr;
};
FiberProbe* fiber_probe = nullptr;
void SwitchTestFiber(void* next) { hulk::SuspendSimulationTick suspended; SwitchToFiber(next); }
float ReadClock(X86Context ctx, uint8_t* base) {
    ctx.ecx = main_clock; ctx.esp = 0x9000;
    ELAPSED(ctx, base);
    return float(ctx.st[ctx.top]);
}
void WINAPI ProbeFiber(void* parameter) {
    auto& probe = *static_cast<FiberProbe*>(parameter);
    probe.child_raw = ReadClock(probe.snapshot, probe.base) == raw_delta;
    SwitchTestFiber(probe.root);
    probe.resumed_raw = ReadClock(probe.snapshot, probe.base) == raw_delta;
    SwitchTestFiber(probe.root);
    std::terminate();
}
}
namespace hulk { float TestSimulationFrameElapsed() { return wall_delta; } }
X86_FUNC(ORIGINAL_ELAPSED) {
    if (ctx.ecx == alternate_clock) { ++alternate_reads; x87_push(ctx, alternate_delta); }
    else { ++raw_clock_calls; x87_push(ctx, raw_delta); }
    ctx.esp += 4;
}
X86_FUNC(ORIGINAL_UNSCALED) {
    x87_push(ctx, ctx.ecx == alternate_clock ? alternate_delta : raw_delta);
    ctx.esp += 4;
}
X86_FUNC(ORIGINAL_WORLD) {
    ++world_calls;
    const uint32_t stack = ctx.esp;
    if (X86_MEM32(X86_MEM32(stack + 4)) != 6 && X86_MEM32(X86_MEM32(stack + 4)) != 7) {
        if (std::bit_cast<float>(uint32_t(X86_MEM32(stack + 8))) != expected_delta) bad_tick = true;
        auto getter = ctx;
        getter.ecx = main_clock;
        getter.esp = 0x9000;
        ELAPSED(getter, base);
        if (getter.esp != 0x9004 || getter.st[getter.top] != expected_delta) bad_tick = true;
        getter = {}; getter.ecx = main_clock; getter.esp = 0x9000;
        UNSCALED(getter, base);
        const float unscaled = expected_delta == 0 ? hulk::SimulationClock::kStep : raw_delta;
        if (getter.esp != 0x9004 || getter.st[getter.top] != unscaled) bad_tick = true;
        for (float delta : {0.0f, 1.0f / 240}) {
            alternate_delta = delta;
            getter = {}; getter.ecx = alternate_clock; getter.esp = 0x9000;
            ELAPSED(getter, base);
            if (getter.esp != 0x9004 || getter.st[getter.top] != delta) bad_tick = true;
            getter = {}; getter.ecx = alternate_clock; getter.esp = 0x9000;
            UNSCALED(getter, base);
            if (getter.esp != 0x9004 || getter.st[getter.top] != delta) bad_tick = true;
        }
        if (fiber_probe && !fiber_probe->entered) {
            auto& probe = *fiber_probe;
            probe.entered = true; probe.snapshot = ctx; probe.base = base;
            probe.converted = !IsThreadAFiber();
            probe.root = probe.converted ? ConvertThreadToFiberEx(nullptr, FIBER_FLAG_FLOAT_SWITCH) : GetCurrentFiber();
            probe.child = CreateFiberEx(65536, 1024 * 1024, FIBER_FLAG_FLOAT_SWITCH, ProbeFiber, &probe);
            if (!probe.root || !probe.child) throw std::runtime_error("cannot create isolated test fibers");
            SwitchTestFiber(probe.child);
            probe.parent_fixed = ReadClock(ctx, base) == expected_delta;
        }
        // The original world routine overwrites its two argument slots before returning.
        X86_MEM32(stack + 4) = 0xdeadbeef;
        X86_MEM32(stack + 8) = 0xdeadbeef;
        if (throw_tick) throw std::runtime_error("guest continuation unwind");
    }
    ctx.esp += 12;
}

static bool FiberScopeTests() {
    std::vector<uint8_t> memory(0x600000);
    auto* base = memory.data();
    X86_MEM32(hulk::addr::kApp) = 0x1000;
    X86_MEM32(0x1058) = main_clock;
    FiberProbe probe;
    fiber_probe = &probe;
    expected_delta = hulk::SimulationClock::kStep;
    X86Context ctx{}; ctx.ecx = 0xb000; ctx.esp = 0x8000;
    X86_MEM32(0x100) = 4; X86_MEM32(0x8004) = 0x100;
    X86_MEM32(0x8008) = std::bit_cast<uint32_t>(expected_delta);
    WORLD(ctx, base);
    SwitchTestFiber(probe.child);
    DeleteFiber(probe.child);
    if (probe.converted && !ConvertFiberToThread()) throw std::runtime_error("cannot restore test thread");
    fiber_probe = nullptr;
    const bool ok = probe.child_raw && probe.parent_fixed && probe.resumed_raw;
    std::printf("native fiber clock isolation: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

static bool WrapperTests() {
    std::vector<uint8_t> memory(0x600000);
    uint8_t* base = memory.data();
    X86_MEM32(hulk::addr::kApp) = 0x1000;
    X86_MEM32(0x1058) = main_clock;
    uint32_t owner = 100;
    const auto update = [&](float dt, uint32_t state = 4) {
        X86Context ctx{};
        ctx.esp = 0x8000; ctx.ecx = owner;
        X86_MEM32(0x100) = state;
        X86_MEM32(0x8004) = 0x100;
        X86_MEM32(0x8008) = std::bit_cast<uint32_t>(dt);
        WORLD(ctx, base);
        return ctx.esp == 0x800c && X86_MEM32(0x8004) == 0x100 && X86_MEM32(0x8008) == std::bit_cast<uint32_t>(dt);
    };
    const auto raw = [&] {
        X86Context ctx{}; ctx.esp = 0x9000; ctx.ecx = main_clock;
        ELAPSED(ctx, base);
        const bool scaled_ok = ctx.esp == 0x9004 && ctx.st[ctx.top] == raw_delta;
        ctx = {}; ctx.esp = 0x9000; ctx.ecx = main_clock;
        UNSCALED(ctx, base);
        return scaled_ok && ctx.esp == 0x9004 && ctx.st[ctx.top] == raw_delta;
    };
    if (!raw()) return false;
    for (unsigned fps : {30u, 60u, 120u, 144u}) {
        ++owner; world_calls = 0;
        for (unsigned i = 0; i < fps * 10; ++i) if (!update(1.0f / fps)) return false;
        if (world_calls < 599 || world_calls > 600 || bad_tick) return false;
    }
    // Zero-time housekeeping is 60 Hz at each cap; both world argument and clock read stay zero.
    expected_delta = 0;
    for (unsigned fps : {60u, 120u, 144u}) {
        ++owner; world_calls = 0; wall_delta = 1.0f / fps;
        for (unsigned i = 0; i < fps * 10; ++i) if (!update(0)) return false;
        if (world_calls < 598 || world_calls > 600 || bad_tick) return false;
    }
    wall_delta = 0; world_calls = 0;
    if (!update(0) || world_calls || !update(-1) || !update(std::numeric_limits<float>::quiet_NaN()) || world_calls) return false;
    // Preserve the original separate non-world states without accidentally entering the clock scope.
    if (!update(0, 6) || !update(0, 7) || world_calls != 2 || !raw()) return false;
    expected_delta = hulk::SimulationClock::kStep;
    world_calls = 0;
    if (!update(hulk::SimulationClock::kStep * 4) || world_calls != 4 || bad_tick) return false;
    // A short pause must retain active fractional time without advancing it during housekeeping.
    ++owner; world_calls = 0;
    if (!update(hulk::SimulationClock::kStep * .5f) || !update(0) || !update(hulk::SimulationClock::kStep * .5f) || world_calls != 1) return false;
    ++owner; world_calls = 0;
    if (!update(hulk::SimulationClock::kStep * .5f) || world_calls) return false;
    // Continuation/exception unwinding must restore both arguments and the clock override scope.
    throw_tick = true;
    try { update(hulk::SimulationClock::kStep); return false; }
    catch (const std::runtime_error&) {}
    throw_tick = false;
    if (X86_MEM32(0x8004) != 0x100 || X86_MEM32(0x8008) != std::bit_cast<uint32_t>(hulk::SimulationClock::kStep) || !raw()) return false;
    return !bad_tick && raw_clock_calls == 3 && alternate_reads > 0;
}

static bool MixedDeltaTests() {
    bool ok = true;
    for (unsigned fps : {60u, 120u, 144u}) {
        hulk::WorldSimulationClock clock;
        unsigned active = 0, housekeeping = 0;
        for (unsigned i = 0; i < fps * 10; ++i) {
            const auto step = clock.Advance(i % 2 ? 0 : 1.0f / fps, 1.0f / fps);
            (step.elapsed > 0 ? active : housekeeping) += step.count;
        }
        std::printf("mixed %u fps: %u active, %u housekeeping ticks\n", fps, active, housekeeping);
        ok &= active >= 299 && active <= 300 && housekeeping >= 299 && housekeeping <= 300;
    }
    return ok;
}

int main() {
    for (unsigned fps : {30u, 60u, 120u, 144u}) {
        hulk::SimulationClock clock;
        unsigned ticks = 0;
        for (unsigned i = 0; i < fps * 120; ++i) ticks += clock.Advance(1.0f / fps);
        // Float input quantization may leave at most one fractional tick after two minutes.
        if (ticks < 7199 || ticks > 7200) { std::printf("%u fps: %u ticks\n", fps, ticks); return 1; }
    }
    hulk::SimulationClock clock;
    unsigned ticks = 0;
    // Alternating frame times with the same total elapsed time must preserve game speed.
    for (unsigned i = 0; i < 600; ++i) { ticks += clock.Advance(0.005f); ticks += clock.Advance(0.015f); }
    if (ticks < 719 || ticks > 720) return 2;
    if (clock.Advance(10.0f) != 4 || clock.Advance(0) != 0) return 3;
    if (clock.Advance(std::numeric_limits<float>::quiet_NaN()) != 0) return 4;
    if (clock.Advance(-1) != 0 || clock.Advance(hulk::SimulationClock::kStep) != 1) return 5;
    const bool mixed = MixedDeltaTests(), wrapper = WrapperTests(), fibers = FiberScopeTests();
    if (!mixed || !wrapper || !fibers) {
        std::printf("mixed regression: %s; wrapper/receiver regression: %s\n", mixed ? "PASS" : "FAIL", wrapper ? "PASS" : "FAIL");
        return 6;
    }
    return 0;
}
