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
X86_FUNC(sub_00026C30) {
    static const bool enabled = std::getenv("HULK_TEST_VEHICLES") != nullptr;
    const uint32_t out = X86_MEM32(ctx.esp + 4);
    const uint32_t name = X86_MEM32(ctx.esp + 8);
    __imp__sub_00026C30(ctx, base);
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
