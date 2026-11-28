// Hulk Recompiled's script plugin API: the C ABI a mod is written against (docs/plugin-api.md).
// Standalone on purpose: any C or C++ compiler can include it, nothing here pulls the game in.
// A plugin is a 64-bit Windows DLL (mods\ExampleTrainer.dll, or mods\My Mod\ExampleTrainer.dll)
// exporting HulkPluginQuery, which returns its HulkPluginInfo. The host checks api_version first,
// then calls OnLoad with the HulkHostApi the plugin keeps for its whole life, ticks OnTick once per
// world update step, and calls OnUnload before it is let go. Build it 64-bit: the game is, and a
// 32-bit DLL does not load into it.
//
// API v2 appends the game's own Lua 5.1 (LuaReady, RunLua, LuaGetNumber, LuaGetString, LuaLastError):
// a chunk of the plugin's runs on the VM the disc's scripts run on, with the engine's whole script API.
// The fields are appended and the host accepts version 1 as well, so a v1 plugin keeps loading unchanged.
// A mod that only needs Lua does not need a DLL at all (docs/lua-mods.md).
//
//           plugins\ExampleTrainer.dll        one plugin, a file
//           plugins\My Mod\Trainer.dll        one plugin in its own folder, beside its data files
//           plugins\_Trainer.dll              a file whose name starts with _ or . is left out
//           plugins\_My Mod\                  a folder whose name starts with _ or . is left out
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HULK_PLUGIN_API_VERSION 2

#if defined(_WIN32)
#define HULK_PLUGIN_EXPORT __declspec(dllexport)
#define HULK_PLUGIN_CALL __cdecl  // the exported query's convention, explicit for an x86 build
#else
#define HULK_PLUGIN_EXPORT __attribute__((visibility("default")))
#define HULK_PLUGIN_CALL
#endif

typedef struct HulkVec3 {
    float x, y, z;
} HulkVec3;

// The host's own, handed to OnLoad and good for the plugin's whole life. Everything runs on the
// game's update thread: call from OnTick only, the one place the game's state is settled.
typedef struct HulkHostApi {
    uint32_t api_version;                // HULK_PLUGIN_API_VERSION the host was built with

    void (*Log)(const char* fmt, ...);   // the game's log, logs\hulk.log

    HulkVec3 (*GetHulkPos)(void);        // the player's position, all zero when there is no player
    void (*SetHulkPos)(HulkVec3 pos);    // moves him; ignored when he has no matrix to move
    float (*GetHealth)(void);            // his power, 0 outside the game
    void (*SetHealth)(float health);     // through the game's own setter, a dead character stays dead
    float (*GetMaxHealth)(void);         // the most his power can be, 0 outside the game
    float (*GetRage)(void);              // the power above his Critical Mass threshold, 0 below it
    void (*SetRage)(float rage);         // the rage above the threshold, up to the maximum
    void (*SetGameSpeed)(float scale);   // the world clock's scale, 1 is the game's own speed

    // the game's memory at a guest address, 0 when any page the range touches is not committed
    int (*ReadMem)(uint32_t va, void* dst, uint32_t n);
    int (*WriteMem)(uint32_t va, const void* src, uint32_t n);

    // runs the game's own code at a guest address with stdcall arguments (OnTick's thread only)
    uint32_t (*CallGuest)(uint32_t fn, const uint32_t* args, uint32_t count);

    // ---- v2: the game's own Lua 5.1, the VM its own scripts run on (src\common\lua_bridge.h). Every call here is
    // an OnTick call: the VM is only settled on the game's update thread. The state is re-read on each call, so a
    // pointer is never stale across a mission change. Appended, so a v1 plugin keeps working without these.
    int (*LuaReady)(void);                                  // 1 when the game has a live Lua state
    int (*RunLua)(const char* src, const char* chunkname);   // 0 ran, 1 did not compile, 2 raised, -1 did not run
    int (*LuaGetNumber)(const char* expr, double* out);      // 1 and *out when expr is a number
    int (*LuaGetString)(const char* expr, char* buf, int cap);  // 1 and the value in buf when it is a string
    const char* (*LuaLastError)(void);                       // the last failing call's message, "" when none
} HulkHostApi;

typedef struct HulkPluginInfo {
    uint32_t api_version;      // 1..HULK_PLUGIN_API_VERSION, or the plugin is not loaded
    const char* name;          // shown in the log; the file name when it is not there
    const char* author;        // optional
    const char* version;       // optional
    const char* description;   // optional
    void (*OnLoad)(HulkHostApi* host);
    void (*OnTick)(float dt);  // each world update step, its dt
    void (*OnUnload)(void);
} HulkPluginInfo;

// every plugin exports this one function, which returns its info
HULK_PLUGIN_EXPORT const HulkPluginInfo* HULK_PLUGIN_CALL HulkPluginQuery(void);

#ifdef __cplusplus
}  // extern "C"
#endif
