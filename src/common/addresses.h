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
#define HULK_SUB_AUDIO_QUALITY sub_000BBD10
#define HULK_SUB_PRINT sub_000B45E0
#define HULK_SUB_MENU_PROMPT sub_000BC900   // the frontend's bottom prompt
#define HULK_SUB_SWITCH_TO_FIBER sub_0035551D
#define HULK_SUB_DELETE_FIBER sub_0035550A
#define HULK_SUB_CAMERA_INVERT_X sub_000BBD80  // CameraInvertXChanged
#define HULK_SUB_CAMERA_INVERT_Y sub_000BBDA0  // CameraInvertYChanged
#define HULK_SUB_HINT_BEGIN sub_000CE700       // hud_triggerButtonHintBegin
#define HULK_SUB_GUARDED_CALL_A sub_002152F0   // the guarded call whose resume point is 0x215318
#define HULK_SUB_GUARDED_CALL_B sub_002D6F10   // the guarded call whose resume point is 0x2D6F32
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
#define HULK_SUB_AUDIO_QUALITY sub_000BBEC0
#define HULK_SUB_PRINT sub_000B46B0
#define HULK_SUB_MENU_PROMPT sub_000BCA60
#define HULK_SUB_SWITCH_TO_FIBER sub_00350EBD
#define HULK_SUB_DELETE_FIBER sub_00350EAA
#define HULK_SUB_CAMERA_INVERT_X sub_000BBF30
#define HULK_SUB_CAMERA_INVERT_Y sub_000BBF50
#define HULK_SUB_HINT_BEGIN sub_000CE870       // hud_triggerButtonHintBegin
#define HULK_SUB_GUARDED_CALL_A sub_0032DC20   // the guarded call whose resume point is 0x32DC48
#define HULK_SUB_GUARDED_CALL_B sub_002B9850   // the guarded call whose resume point is 0x2B9872
#endif
}  // namespace hulk::addr

// a hooked function's original, __imp__ and its name
#define HULK_IMP(f) HULK_IMP_(f)
#define HULK_IMP_(f) __imp__##f
