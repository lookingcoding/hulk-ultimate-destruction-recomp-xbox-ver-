// Opt-in European-disc diagnostics for issue #10. Reads the result of the game's own
// actor-position query; does not change actor state. The test mission requests a query
// once per second, and ordinary play never enables HULK_TEST_VEHICLES.
#ifndef HULK_USA
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include "core/log.h"
#include "xdk/hle.h"

X86_EXTERN_FUNC(__imp__sub_00026C30);
namespace {
thread_local bool querying_truck = false;
uint32_t truck_character = 0;
float FloatAt(uint32_t address, uint8_t* base) { return std::bit_cast<float>(uint32_t(X86_MEM32(address))); }
float TruckY(uint32_t actor, uint8_t* base) {
    const uint32_t p = X86_MEM32(actor + 0x10), matrix = X86_MEM32(p + 0x18);
    return FloatAt(matrix ? matrix + 0x44 : p + 8, base);
}
}
X86_EXTERN_FUNC(__imp__sub_00024280);
X86_FUNC(sub_00024280) {
    __imp__sub_00024280(ctx, base);
    if (querying_truck && ctx.eax) {
        static uint32_t last = 0;
        if (last != ctx.eax) {
            last = ctx.eax;
            truck_character = last;
            const uint32_t spatial = X86_MEM32(last + 0x10), controller = X86_MEM32(last + 0x11c);
            LOGI("vehicle diagnostic: character=%08X vtable=%08X spatial=%08X vt=%08X controller=%08X vt=%08X",
                 last, X86_MEM32(last), spatial, spatial ? X86_MEM32(spatial) : 0, controller, controller ? X86_MEM32(controller) : 0);
        }
    }
}

// Temporary opt-in phase tracing: identify the routine producing vertical drift.
#define TRACE_TRUCK_PHASE(address, label, dt_offset) \
X86_EXTERN_FUNC(__imp__sub_##address); \
X86_FUNC(sub_##address) { \
    const uint32_t actor = ctx.ecx; \
    const bool trace = actor && actor == truck_character; \
    const float before = trace ? TruckY(actor, base) : 0; \
    const float dt = trace ? FloatAt(ctx.esp + dt_offset, base) : 0; \
    __imp__sub_##address(ctx, base); \
    if (trace) { \
        static float sum = 0; static unsigned calls = 0; \
        sum += TruckY(actor, base) - before; ++calls; \
        static auto next = std::chrono::steady_clock::now(); \
        const auto now = std::chrono::steady_clock::now(); \
        if (now >= next) { \
            LOGI("truck phase: " label " calls=%u dy=%.6f dt=%.6f y=%.4f velocity_y=%.4f", calls, sum, dt, TruckY(actor, base), FloatAt(actor + 0x84, base)); \
            next = now + std::chrono::seconds(1); calls = 0; sum = 0; \
        } \
    } \
}
TRACE_TRUCK_PHASE(00040950, "movement", 8)
TRACE_TRUCK_PHASE(000418D0, "ground", 8)
TRACE_TRUCK_PHASE(0003E270, "sleepwalk", 20)
X86_FUNC(sub_00026C30) {
    static const bool enabled = std::getenv("HULK_TEST_VEHICLES") != nullptr;
    const uint32_t out = X86_MEM32(ctx.esp + 4);
    const uint32_t name = X86_MEM32(ctx.esp + 8);
    querying_truck = enabled && name && std::strstr(reinterpret_cast<const char*>(base + name), "division_truck_c3m3");
    __imp__sub_00026C30(ctx, base);
    querying_truck = false;
    if (!enabled || !out || !name) return;
    const char* actor = reinterpret_cast<const char*>(base + name);
    if (!std::strstr(actor, "truck") && !std::strstr(actor, "Truck")) return;
    using Clock = std::chrono::steady_clock;
    static std::unordered_map<std::string, Clock::time_point> next;
    const auto now = Clock::now();
    auto& when = next[actor];
    if (now < when) return;
    when = now + std::chrono::seconds(1);
    const auto coord = [&](uint32_t at) { return std::bit_cast<float>(uint32_t(X86_MEM32(out + at))); };
    LOGI("vehicle probe: %s position %.3f %.3f %.3f", actor, coord(0), coord(4), coord(8));
}
#endif
