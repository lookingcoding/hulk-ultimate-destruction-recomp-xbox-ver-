#include "common/plugins.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include "app/app.h"
#include "common/addresses.h"
#include "common/lua_bridge.h"
#include "common/replay.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace hulk::plugins {
namespace {

namespace fs = std::filesystem;

// a plugin that faulted this many times is left out: one fault is logged, three end it
constexpr unsigned kMaxFaults = 3;
constexpr uint32_t kMaxGuestArgs = 16;
constexpr uint32_t kMaxMemBytes = 0x01000000;  // one read or write, never a range that wraps

struct Plugin {
    HMODULE module = nullptr;
    const HulkPluginInfo* info = nullptr;
    std::string name;   // its info's, or the file's
    fs::path path;
    unsigned faults = 0;
    bool dead = false;  // faulted out: ticked and unloaded no more
};

// the plugin a fault is counted against, set around each call into one
Plugin* g_calling = nullptr;

// ---------------------------------------------------------------- the host API's own

void HostLog(const char* fmt, ...);
HulkVec3 GetHulkPos();
void SetHulkPos(HulkVec3 pos);
float GetHealth();
void SetHealth(float health);
float GetMaxHealth();
float GetRage();
void SetRage(float rage);
void SetGameSpeed(float scale);
int ReadMem(uint32_t va, void* dst, uint32_t n);
int WriteMem(uint32_t va, const void* src, uint32_t n);
uint32_t CallGuest(uint32_t fn, const uint32_t* args, uint32_t count);
int LuaReady();
int RunLua(const char* src, const char* chunkname);
int LuaGetNumber(const char* expr, double* out);
int LuaGetString(const char* expr, char* buf, int cap);
const char* LuaLastError();

HulkHostApi g_host = [] {
    HulkHostApi host {};
    host.api_version = HULK_PLUGIN_API_VERSION;
    host.Log = &HostLog;
    host.GetHulkPos = &GetHulkPos;
    host.SetHulkPos = &SetHulkPos;
    host.GetHealth = &GetHealth;
    host.SetHealth = &SetHealth;
    host.GetMaxHealth = &GetMaxHealth;
    host.GetRage = &GetRage;
    host.SetRage = &SetRage;
    host.SetGameSpeed = &SetGameSpeed;
    host.ReadMem = &ReadMem;
    host.WriteMem = &WriteMem;
    host.CallGuest = &CallGuest;
    host.LuaReady = &LuaReady;
    host.RunLua = &RunLua;
    host.LuaGetNumber = &LuaGetNumber;
    host.LuaGetString = &LuaGetString;
    host.LuaLastError = &LuaLastError;
    return host;
}();

std::mutex g_lock;
std::vector<Plugin> g_plugins;
bool g_scanned = false;

// the player's character: the app's actor manager (+0x20) and its main actor (+0x124), 0 outside the
// game (game/cheat_stats.cpp)
uint32_t Player() {
    if (!mem::Base()) return 0;
    const uint32_t app = kernel::R32(addr::kApp);
    const uint32_t actors = app ? kernel::R32(app + 0x20) : 0;
    return actors ? kernel::R32(actors + 0x124) : 0;
}

// every page the range touches, and never one that would wrap past the address space's end
bool Committed(uint32_t va, uint32_t n) {
    if (!n || n > kMaxMemBytes || va > 0xFFFFFFFFu - n) return false;
    for (uint32_t a = va & ~uint32_t(mem::kPage - 1);; a += mem::kPage) {
        if (!mem::IsCommitted(a)) return false;
        if (a >= va + n - 1) return true;
    }
}

// the game's memory at a guest address, 0 when the bytes are not committed
float F32Of(uint32_t va) { return Committed(va, 4) ? std::bit_cast<float>(kernel::R32(va)) : 0; }

void HostLog(const char* fmt, ...) {
    char line[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof(line), fmt ? fmt : "(null)", args);
    va_end(args);
    LOGR("plugins: %s", line);
}

// the player's spatial (character +0x10) keeps its matrix (+0x18), whose translation is at +0x40/44/48:
// y is the offset the movement audit reads (game/cheat_movement.cpp), x and z sit beside it in the same
// vector. No matrix: y at spatial+0x08, and x and z are not there to be had.
HulkVec3 GetHulkPos() {
    HulkVec3 out = {0, 0, 0};
    const uint32_t player = Player();
    if (!player) return out;
    const uint32_t spatial = kernel::R32(player + 0x10);
    if (!spatial) return out;
    const uint32_t matrix = kernel::R32(spatial + 0x18);
    if (!matrix) {
        out.y = F32Of(spatial + 0x08);
        return out;
    }
    out.x = F32Of(matrix + 0x40);
    out.y = F32Of(matrix + 0x44);
    out.z = F32Of(matrix + 0x48);
    return out;
}

void SetHulkPos(HulkVec3 pos) {
    const uint32_t player = Player();
    if (!player) return;
    const uint32_t spatial = kernel::R32(player + 0x10);
    if (!spatial) return;
    const uint32_t matrix = kernel::R32(spatial + 0x18);
    if (!matrix) return;  // a half a position is not worth writing: x and z need the matrix
    const float v[3] = {pos.x, pos.y, pos.z};
    for (int i = 0; i < 3; i++)
        if (std::isfinite(v[i])) WriteMem(matrix + 0x40 + i * 4, &v[i], 4);
}

float GetHealth() { return F32Of(Player() + 0x1B0C); }  // the character's power (kHealth)

// through the game's own setter (thiscall value, force), as the health cheats do it: a dead character
// stays dead, and the setter is called on the guest's update thread, from OnTick
void SetHealth(float health) {
    const uint32_t player = Player();
    if (!player || !std::isfinite(health) || health < 0) return;
    kernel::Thread* t = kernel::Current();
    if (!t || !t->ctx) return;
    X86Context& ctx = *t->ctx;
    ctx.ecx = player;
    kernel::CallGuest(ctx, addr::kSetHealth, {std::bit_cast<uint32_t>(health), 1});
}

float GetMaxHealth() { return F32Of(Player() + 0x1B10); }

// rage is the power above the character's Critical Mass threshold (+0x1B14): 0 below it, where he is out
// of the state the mode at +0x1B28 keeps
float GetRage() {
    const uint32_t player = Player();
    if (!player) return 0;
    const float health = F32Of(player + 0x1B0C), threshold = F32Of(player + 0x1B14);
    return health > threshold ? health - threshold : 0;
}

// the rage above the threshold, set through the game's own health setter so Critical Mass comes with it,
// up to the maximum the power can be
void SetRage(float rage) {
    const uint32_t player = Player();
    if (!player || !std::isfinite(rage) || rage < 0) return;
    SetHealth(std::min(F32Of(player + 0x1B14) + rage, F32Of(player + 0x1B10)));
}

// the world clock's scale (the app's timer at +0x58, Q16 at +0x38), through the game's own setter as the
// GameSpeed cheat sets it. The scale a plugin wrote is kept as the base the cheat scales from.
void SetGameSpeed(float scale) {
    if (!std::isfinite(scale) || scale <= 0) return;
    scale = std::clamp(scale, 0.01f, 100.0f);
    const uint32_t app = kernel::R32(addr::kApp);
    const uint32_t timer = app ? kernel::R32(app + 0x58) : 0;
    if (!timer) return;
    static uint32_t written = 0, timer_seen = 0;  // already set: the game's clock is left as it is
    if (timer != timer_seen) timer_seen = timer, written = 0;
    const uint32_t bits = std::bit_cast<uint32_t>(scale);
    if (bits == written) return;
    kernel::Thread* t = kernel::Current();
    if (!t || !t->ctx) return;
    X86Context& ctx = *t->ctx;
    ctx.ecx = timer;
    kernel::CallGuest(ctx, addr::kTimerSetScale, {bits});
    written = kernel::R32(timer + 0x38);
    LOGI("plugins: game speed %.3f (was %.3f)", scale, written / 65536.0);
}

int ReadMem(uint32_t va, void* dst, uint32_t n) {
    uint8_t* base = mem::Base();
    if (!base || !dst || !Committed(va, n)) return 0;
    std::memcpy(dst, base + va, n);
    return 1;
}

int WriteMem(uint32_t va, const void* src, uint32_t n) {
    uint8_t* base = mem::Base();
    if (!base || !src || !Committed(va, n)) return 0;
    std::memcpy(base + va, src, n);
    return 1;
}

// the game's own code at a guest address, its stdcall arguments pushed, on the update thread
uint32_t CallGuest(uint32_t fn, const uint32_t* args, uint32_t count) {
    if (count > kMaxGuestArgs) {
        LOGW("plugins: a guest call with %u arguments is too many, ignored", count);
        return 0;
    }
    uint32_t stack[kMaxGuestArgs] = {};
    for (uint32_t i = 0; i < count; i++) stack[i] = args[i];
    kernel::Thread* t = kernel::Current();
    if (!t || !t->ctx) {
        LOG_FIRST(1, "plugins: a guest call outside the update thread is not supported, ignored");
        return 0;
    }
    if (!mem::IsCommitted(fn)) {
        LOGW("plugins: a guest call to %08X, which is not committed, ignored", fn);
        return 0;
    }
    return kernel::CallGuest(*t->ctx, fn, stack, count);
}

// ---------------------------------------------------------------- v2: the game's Lua (common/lua_bridge.cpp)

int LuaReady() { return hulk::lua::Ready() ? 1 : 0; }

int RunLua(const char* src, const char* chunkname) {
    if (!src) return -1;
    return hulk::lua::Run(src, std::strlen(src), chunkname && *chunkname ? chunkname : "@plugin");
}

int LuaGetNumber(const char* expr, double* out) {
    float value = 0;
    if (!out || !hulk::lua::GetNumber(expr, &value)) return 0;
    *out = double(value);
    return 1;
}

int LuaGetString(const char* expr, char* buf, int cap) { return hulk::lua::GetString(expr, buf, cap) ? 1 : 0; }

const char* LuaLastError() { return hulk::lua::LastError(); }

// ---------------------------------------------------------------- faults

// the filter records the fault against the plugin the call was made for, and disables it once it has
// faulted enough times
unsigned RecordFault(unsigned long code) {
    Plugin& p = *g_calling;
    p.faults++;
    if (p.faults < kMaxFaults)
        LOGE("plugins: %s faulted (%08lX), %u of %u", p.name.c_str(), code, p.faults, kMaxFaults);
    else {
        p.dead = true;
        LOGE("plugins: %s faulted (%08lX) %u times, disabled", p.name.c_str(), code, p.faults);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// calls into a plugin, 1 when it faulted. The callback's own arguments are plain, so the handler stays
// out of any C++ unwinding; the guest reads are caught the same way (common/keyboard.cpp).
__declspec(noinline) unsigned CallLoad(void (*fn)(HulkHostApi*), HulkHostApi* host) {
    __try {
        fn(host);
        return 0;
    } __except (RecordFault(GetExceptionCode())) {
        return 1;
    }
}
__declspec(noinline) unsigned CallTick(void (*fn)(float), float dt) {
    __try {
        fn(dt);
        return 0;
    } __except (RecordFault(GetExceptionCode())) {
        return 1;
    }
}
__declspec(noinline) unsigned CallUnload(void (*fn)(void)) {
    __try {
        fn();
        return 0;
    } __except (RecordFault(GetExceptionCode())) {
        return 1;
    }
}

// ---------------------------------------------------------------- loading

// the mods folder beside the assets folder: beside the exe in a release, above it in the build output
fs::path ModsDir() {
    fs::path dir = app::ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "mods";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

std::string LowerExt(const fs::path& path) {
    std::string ext = path.extension().string();
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext;
}

bool IsPluginFile(const fs::path& path, std::error_code& ec) {
    if (!fs::is_regular_file(path, ec) || ec) return false;
    const std::string ext = LowerExt(path);
    return ext == ".dll" || ext == ".asi";
}

// every plugin file of a folder: its own *.dll and *.asi, a name or folder starting with _ or . off
void Collect(const fs::path& dir, std::vector<fs::path>& files) {
    std::error_code ec;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '_' || name[0] == '.') continue;
        if (IsPluginFile(e.path(), ec)) files.push_back(e.path());
    }
}

void LoadPlugin(const fs::path& path) {
    HMODULE module = LoadLibraryW(path.c_str());
    if (!module) {
        LOGW("plugins: cannot load %s (%lu)", path.string().c_str(), GetLastError());
        return;
    }
    using QueryFn = const HulkPluginInfo* ();
    // cdecl exports on x86 may carry a leading underscore, depending on the linker
    auto query = reinterpret_cast<QueryFn*>(reinterpret_cast<void*>(GetProcAddress(module, "HulkPluginQuery")));
    if (!query)
        query = reinterpret_cast<QueryFn*>(reinterpret_cast<void*>(GetProcAddress(module, "_HulkPluginQuery")));
    const HulkPluginInfo* info = query ? query() : nullptr;
    if (!info || !info->api_version || info->api_version > HULK_PLUGIN_API_VERSION) {
        LOGW("plugins: %s is built for API version %u, this game has 1 to %u, left out",
             path.filename().string().c_str(), info ? info->api_version : 0, HULK_PLUGIN_API_VERSION);
        FreeLibrary(module);
        return;
    }
    Plugin plugin;
    plugin.module = module;
    plugin.info = info;
    plugin.name = info->name ? info->name : path.stem().string();
    plugin.path = path;
    LOGR("plugins: %s%s%s from %s", plugin.name.c_str(), info->version ? " " : "",
         info->version ? info->version : "", path.string().c_str());
    // OnLoad is caught like OnTick is: a faulting plugin is disabled before it can hurt the game
    g_calling = &plugin;
    const unsigned faulted = info->OnLoad ? CallLoad(info->OnLoad, &g_host) : 0;
    g_calling = nullptr;
    if (faulted) {
        FreeLibrary(module);
        return;
    }
    g_plugins.push_back(std::move(plugin));
}

}  // namespace

void Load() {
    // A background test drives the game through its own hidden window and internal input: the plugins
    // are skipped entirely (replay.h), the same as its pad is.
    if (hulk::BackgroundTest()) return;
    std::lock_guard guard(g_lock);
    if (g_scanned) return;
    g_scanned = true;
    const fs::path dir = ModsDir();
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        fs::create_directories(dir, ec);
        if (ec || !fs::is_directory(dir, ec)) return;
    }
    std::vector<fs::path> files;
    Collect(dir, files);
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec)) continue;
        const std::string name = e.path().filename().string();
        if (name.empty() || name[0] == '_' || name[0] == '.') continue;
        Collect(e.path(), files);
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        std::string la = a.string(), lb = b.string();
        for (char& c : la) c = char(std::tolower(uint8_t(c)));
        for (char& c : lb) c = char(std::tolower(uint8_t(c)));
        return la < lb;
    });
    for (const fs::path& path : files) LoadPlugin(path);
    if (!g_plugins.empty()) LOGR("plugins: %zu loaded in %s", g_plugins.size(), dir.string().c_str());
}

void Tick(float dt) {
    std::lock_guard guard(g_lock);
    if (g_plugins.empty()) return;
    for (Plugin& plugin : g_plugins) {
        if (plugin.dead || !plugin.info->OnTick) continue;
        g_calling = &plugin;
        CallTick(plugin.info->OnTick, dt);
        g_calling = nullptr;
    }
}

void Unload() {
    std::lock_guard guard(g_lock);
    if (g_plugins.empty()) return;
    for (Plugin& plugin : g_plugins) {
        if (plugin.dead) continue;
        if (plugin.info->OnUnload) CallUnload(plugin.info->OnUnload);
        FreeLibrary(plugin.module);
        plugin.module = nullptr;
    }
    g_plugins.clear();
    LOGR("plugins: unloaded");
}

}  // namespace hulk::plugins
