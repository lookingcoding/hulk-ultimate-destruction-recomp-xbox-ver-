#pragma once
#include <cstdint>

// What the host code finds in default.xbe by hand. The European release (title 56550039, entry 0020AAC6, the
// HulkRecompiled target) is the default; HULK_USA builds HulkRecompiledUSA for the North American release (USA, Asia;
// entry 0020A016). That build is the same code at other addresses: each USA value is where the European code around
// it matches with the addresses in it masked out, data read from the matching instruction, vtables and type
// descriptors from RTTI. The structures the code uses (FE screens, widgets, functors, fibers) are laid out alike.
namespace hulk::addr {
#ifdef HULK_USA
inline constexpr uint32_t kGuestNew = 0x0008D690;             // operator new, cdecl
inline constexpr uint32_t kRegisterCallback = 0x000FC1E0;     // cdecl (name, functor): a function the scripts call
inline constexpr uint32_t kRegisterIntCallback = 0x000FC200;  // cdecl (name, functor): a named value's int callback
inline constexpr uint32_t kFindWidget = 0x000FC260;           // cdecl (name): the widget of the current screen
inline constexpr uint32_t kDynamicCast = 0x00344E74;          // __RTDynamicCast
inline constexpr uint32_t kFunctorVtable = 0x00409070;        // functor: method (thiscall owner, argc, argv)
inline constexpr uint32_t kIntFunctorVtable = 0x00409080;     // functor: method (thiscall owner, int value)
inline constexpr uint32_t kSharedHandler = 0x000BBD10;        // AudioQualityChanged
inline constexpr uint32_t kSyncHandler = 0x000B45E0;          // Print
inline constexpr uint32_t kWidgetType = 0x0049DBA4;           // FEObject type descriptor
inline constexpr uint32_t kTextSelectorType = 0x0049DBBC;     // TextSelector type descriptor
inline constexpr uint32_t kTextSelectorVtable = 0x0040C4E4;
inline constexpr uint32_t kFrontend = 0x004E0A60;             // the FE manager
inline constexpr uint32_t kApp = 0x004DD700;                  // the app, its flow manager at +0x54
inline constexpr uint32_t kCameraInvertX = 0x004DD83A, kCameraInvertY = 0x004DD83B;
inline constexpr uint32_t kFiberTlsIndex = 0x004E5A9C;
inline constexpr uint32_t kSwitchToFiber = 0x0035551D, kDeleteFiber = 0x0035550A, kFiberBootstrap = 0x00355439;
inline constexpr uint32_t kHintBegin = 0x000CE700;            // hud_triggerButtonHintBegin
#define HULK_SUB_FE_CALLBACKS sub_000C43A0  // the frontend's callbacks object
// cheats (game/cheat_stats.cpp): IsUnlockableOn's gameplay call sites by return address, the damage a character takes,
// its health setter (thiscall value, force) and the world clock's scale setter (thiscall float)
#define HULK_SUB_IS_UNLOCKABLE_ON sub_00092940
#define HULK_SUB_APPLY_DAMAGE sub_00043350
inline constexpr uint32_t kRetInvulnerability = 0x000434FF, kRetDoubleDamage = 0x000434B1;
inline constexpr uint32_t kRetDoubleDamageProps = 0x001D709B, kRetRegenerator = 0x00040BC1;
inline constexpr uint32_t kRetLowGravityCharacter = 0x000826CA, kRetLowGravityWorld = 0x001B5DE4;
inline constexpr uint32_t kRetLowGravityWorldOff = 0x001B5E53;
inline constexpr uint32_t kSetHealth = 0x0003B1C0, kTimerSetScale = 0x00086100;
#define HULK_SUB_AUDIO_QUALITY sub_000BBD10
#define HULK_SUB_PRINT sub_000B45E0
#define HULK_SUB_MENU_PROMPT sub_000BC900   // the frontend's bottom prompt
#define HULK_SUB_SWITCH_TO_FIBER sub_0035551D
#define HULK_SUB_DELETE_FIBER sub_0035550A
#define HULK_SUB_CAMERA_INVERT_X sub_000BBD80  // CameraInvertXChanged
#define HULK_SUB_CAMERA_INVERT_Y sub_000BBDA0  // CameraInvertYChanged
#define HULK_SUB_HINT_BEGIN sub_000CE700       // hud_triggerButtonHintBegin
inline constexpr uint32_t kFrameSwapVBlanks = 0x0055549C;     // vertical blanks since the last swap
inline constexpr uint32_t kFrameSwapOneVBlank = 0x005554A0;   // byte: wait for one blank, not two (loading)
inline constexpr uint32_t kSetTexCoordIndex = 0x00385D40;     // D3DDevice_SetTextureState_TexCoordIndex
inline constexpr uint32_t kTextureStates = 0x0039DB10;        // D3D_DeferredTextureState, [4][32]
#define HULK_SUB_POST_EFFECT sub_00325110      // thiscall: the bloom pass over the finished scene
#define HULK_SUB_FRAME_SWAP sub_0031B190       // thiscall: waits for the blanks, then D3DDevice_Swap
#define HULK_SUB_GUARDED_CALL_A sub_002152F0   // the guarded call whose resume point is 0x215318
#define HULK_SUB_GUARDED_CALL_B sub_002D6F10   // the guarded call whose resume point is 0x2D6F32
inline constexpr uint32_t kRenderManager = 0x004E231C;        // the render manager, made on first use (0x1C3D90)
#define HULK_SUB_RENDER_FRAME sub_001BC720     // the render manager's frame (tail jump to 0x1C3E10)
#define HULK_SUB_SET_VIEW sub_001C1F30         // thiscall render layer (transform, fov, aspect, near, far)
#define HULK_SUB_APPLY_FOG sub_001C2210        // thiscall render layer: the frame's fog and clear colour
#define HULK_SUB_GET_LANGUAGE sub_00209F50     // XGetLanguage: the console's language (language.h)
#define HULK_SUB_CHASE_CAM_UPDATE sub_000A75D0  // thiscall chase camera, its update (vtable +0x20; ret 0x10)
#else
inline constexpr uint32_t kGuestNew = 0x0008D730;
inline constexpr uint32_t kRegisterCallback = 0x000FC400;
inline constexpr uint32_t kRegisterIntCallback = 0x000FC420;
inline constexpr uint32_t kFindWidget = 0x000FC480;
inline constexpr uint32_t kDynamicCast = 0x00340764;
inline constexpr uint32_t kFunctorVtable = 0x00409B30;
inline constexpr uint32_t kIntFunctorVtable = 0x00409B40;
inline constexpr uint32_t kSharedHandler = 0x000BBEC0;
inline constexpr uint32_t kSyncHandler = 0x000B46B0;
inline constexpr uint32_t kWidgetType = 0x0049FCE4;
inline constexpr uint32_t kTextSelectorType = 0x0049FCFC;
inline constexpr uint32_t kTextSelectorVtable = 0x0040CFAC;
inline constexpr uint32_t kFrontend = 0x004E2BA0;
inline constexpr uint32_t kApp = 0x004DF840;
inline constexpr uint32_t kCameraInvertX = 0x004DF97A, kCameraInvertY = 0x004DF97B;
inline constexpr uint32_t kFiberTlsIndex = 0x004E7BDC;
inline constexpr uint32_t kSwitchToFiber = 0x00350EBD, kDeleteFiber = 0x00350EAA, kFiberBootstrap = 0x00350DD9;
inline constexpr uint32_t kHintBegin = 0x000CE870;            // hud_triggerButtonHintBegin
#define HULK_SUB_FE_CALLBACKS sub_000C4500
#define HULK_SUB_IS_UNLOCKABLE_ON sub_000929E0
#define HULK_SUB_APPLY_DAMAGE sub_00043330
inline constexpr uint32_t kRetInvulnerability = 0x000434DF, kRetDoubleDamage = 0x00043491;
inline constexpr uint32_t kRetDoubleDamageProps = 0x001D7ABB, kRetRegenerator = 0x00040BA1;
inline constexpr uint32_t kRetLowGravityCharacter = 0x000826AA, kRetLowGravityWorld = 0x001B6024;
inline constexpr uint32_t kRetLowGravityWorldOff = 0x001B6093;
inline constexpr uint32_t kSetHealth = 0x0003B190, kTimerSetScale = 0x000860E0;
#define HULK_SUB_AUDIO_QUALITY sub_000BBEC0
#define HULK_SUB_PRINT sub_000B46B0
#define HULK_SUB_MENU_PROMPT sub_000BCA60
#define HULK_SUB_SWITCH_TO_FIBER sub_00350EBD
#define HULK_SUB_DELETE_FIBER sub_00350EAA
#define HULK_SUB_CAMERA_INVERT_X sub_000BBF30
#define HULK_SUB_CAMERA_INVERT_Y sub_000BBF50
#define HULK_SUB_HINT_BEGIN sub_000CE870       // hud_triggerButtonHintBegin
inline constexpr uint32_t kFrameSwapVBlanks = 0x004F8020;
inline constexpr uint32_t kFrameSwapOneVBlank = 0x004F8024;
inline constexpr uint32_t kSetTexCoordIndex = 0x00386800;
inline constexpr uint32_t kTextureStates = 0x0039E5D0;
#define HULK_SUB_POST_EFFECT sub_00226720
#define HULK_SUB_FRAME_SWAP sub_0021F300
#define HULK_SUB_GUARDED_CALL_A sub_0032DC20   // the guarded call whose resume point is 0x32DC48
#define HULK_SUB_GUARDED_CALL_B sub_002B9850   // the guarded call whose resume point is 0x2B9872
inline constexpr uint32_t kRenderManager = 0x004E445C;
#define HULK_SUB_RENDER_FRAME sub_001BD100
#define HULK_SUB_SET_VIEW sub_001C2940
#define HULK_SUB_APPLY_FOG sub_001C2C20
#define HULK_SUB_GET_LANGUAGE sub_0020AA00
#define HULK_SUB_CHASE_CAM_UPDATE sub_000A76A0
#endif
}  // namespace hulk::addr

// a hooked function's original, __imp__ and its name
#define HULK_IMP(f) HULK_IMP_(f)
#define HULK_IMP_(f) __imp__##f
