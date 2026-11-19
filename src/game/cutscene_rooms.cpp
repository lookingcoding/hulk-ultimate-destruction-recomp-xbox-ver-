#include "kernel/kernel.h"
#include "xdk/hle.h"

namespace {

#ifdef HULK_USA
#define HULK_SET_CURRENT_ROOM sub_002036E0
#define HULK_SET_CURRENT_ROOM_ORIGINAL __imp__sub_002036E0
constexpr uint32_t kQueueTemporaryRoom = 0x00203780;
constexpr uint32_t kLoadTemporaryRooms = 0x00207BA0;
constexpr uint32_t kReleaseTemporaryRooms = 0x00207D80;
#else
#define HULK_SET_CURRENT_ROOM sub_00204130
#define HULK_SET_CURRENT_ROOM_ORIGINAL __imp__sub_00204130
constexpr uint32_t kQueueTemporaryRoom = 0x002041D0;
constexpr uint32_t kLoadTemporaryRooms = 0x00208610;
constexpr uint32_t kReleaseTemporaryRooms = 0x00208800;
#endif

constexpr uint32_t kGraveyardRoom = 571;
constexpr uint32_t kRoomStatus = 0x5A4;
constexpr uint32_t kTemporaryRoomCount = 0xA4C;
constexpr uint32_t kFirstTemporaryRoom = 0xA50;
constexpr uint32_t kTemporaryRoomsActive = 0xAF5;

thread_local uint32_t graveyardRoomOwner = 0;

}  // namespace

X86_EXTERN_FUNC(HULK_SET_CURRENT_ROOM_ORIGINAL);
X86_FUNC(HULK_SET_CURRENT_ROOM) {
    const uint32_t manager = ctx.ecx;
    const uint32_t room = kernel::R32(ctx.esp + 4);
    HULK_SET_CURRENT_ROOM_ORIGINAL(ctx, base);

    if (room != kGraveyardRoom && manager != graveyardRoomOwner) return;

    const uint8_t* fields = base + manager;
    const uint32_t temporaryCount = kernel::R32(manager + kTemporaryRoomCount);
    const bool temporaryRoomsActive = fields[kTemporaryRoomsActive] != 0;
    if (room == kGraveyardRoom) {
        if (temporaryCount != 0 || temporaryRoomsActive ||
            fields[kRoomStatus + room] != 0) return;

        // The graveyard intro selects room 571 without streaming its scenery.
        // Load it through the game's temporary room batch while the cutscene runs.
        kernel::CallGuest(ctx, kQueueTemporaryRoom, {room});
        kernel::CallGuest(ctx, kLoadTemporaryRooms, {});
        graveyardRoomOwner = manager;
    } else {
        graveyardRoomOwner = 0;
        if (temporaryRoomsActive && temporaryCount == 1 &&
            kernel::R32(manager + kFirstTemporaryRoom) == kGraveyardRoom) {
            kernel::CallGuest(ctx, kReleaseTemporaryRooms, {});
        }
    }
}
