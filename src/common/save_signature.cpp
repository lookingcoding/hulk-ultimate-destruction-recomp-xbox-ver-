#include <algorithm>
#include <cstring>

#include "kernel/crypto.h"
#include "kernel/kernel.h"

namespace {

#ifdef HULK_USA
#define HULK_SIGN_SAVE_CHUNK sub_002781C0
#define HULK_SIGN_SAVE_CHUNK_ORIGINAL __imp__sub_002781C0
constexpr uint32_t kLoadReturn = 0x002795A3;
constexpr uint32_t kChunkBuffer = 0x0052C270;
constexpr uint32_t kChunkIndex = 0x0052C278;
#else
#define HULK_SIGN_SAVE_CHUNK sub_002E07C0
#define HULK_SIGN_SAVE_CHUNK_ORIGINAL __imp__sub_002E07C0
constexpr uint32_t kLoadReturn = 0x002E1BA3;
constexpr uint32_t kChunkBuffer = 0x00525F70;
constexpr uint32_t kChunkIndex = 0x00525F78;
#endif

constexpr uint32_t kChunkSize = 0x2000;
constexpr size_t kSignatureSize = 20;

}  // namespace

X86_EXTERN_FUNC(HULK_SIGN_SAVE_CHUNK_ORIGINAL);
X86_FUNC(HULK_SIGN_SAVE_CHUNK) {
    const uint32_t caller = kernel::R32(ctx.esp);
    const uint32_t fileSize = kernel::R32(ctx.esp + 4);
    const uint32_t signature = kernel::R32(ctx.esp + 8);
    HULK_SIGN_SAVE_CHUNK_ORIGINAL(ctx, base);
    if (caller != kLoadReturn || !(ctx.eax & 0xFF)) return;

    // On load, allow older PC saves signed with the SDK's former zero key.
    // The caller still compares all 20 bytes and handles the result itself.
    const uint64_t chunkOffset = uint64_t(kernel::R32(kChunkIndex)) * kChunkSize;
    if (chunkOffset + kSignatureSize > fileSize) return;
    const size_t payloadSize = std::min<uint64_t>(fileSize - chunkOffset, kChunkSize) - kSignatureSize;
    const auto* payload = static_cast<const uint8_t*>(kernel::HostPtr(kernel::R32(kChunkBuffer)));
    const auto* stored = payload + payloadSize;
    void* expected = kernel::HostPtr(signature);
    if (std::memcmp(stored, expected, kSignatureSize) == 0) return;

    constexpr uint8_t kLegacyKey[16] = {};
    uint8_t legacy[kSignatureSize];
    crypto::Hmac(kLegacyKey, sizeof kLegacyKey, payload, payloadSize, nullptr, 0, legacy);
    if (std::memcmp(stored, legacy, kSignatureSize) == 0)
        std::memcpy(expected, legacy, kSignatureSize);
}
