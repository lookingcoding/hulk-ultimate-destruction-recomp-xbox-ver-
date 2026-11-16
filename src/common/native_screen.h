#pragma once
#include <cstdint>
struct X86Context;
namespace hulk {
// One additional real Dolly screen, owned by its native frontend manager.
// The retail manager's embedded 64 slots remain untouched.
bool ExtraNativeScreen(X86Context& ctx, bool create);
void InitialiseExtraNativeScreen(X86Context& ctx, uint32_t manager);
void ReleaseExtraNativeScreen(X86Context& ctx);
}
