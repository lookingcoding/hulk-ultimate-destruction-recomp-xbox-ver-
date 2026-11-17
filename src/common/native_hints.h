#pragma once
#include <cstdint>
struct X86Context;
namespace hulk {
// The original text pointer is restored after native layout; returned text remains owned until destruction.
void PrepareNativeHint(X86Context& ctx);
void ReleaseNativeHint(X86Context& ctx);
// The footer draws cached Unicode text directly. Returns the temporary frame's stack displacement.
uint32_t PrepareNativePromptDraw(X86Context& ctx);
}
