#include "common/lua_bridge.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <cstring>
#include <string>
#include "common/addresses.h"
#include "core/log.h"
#include "kernel/kernel.h"
#include "kernel/memory.h"

namespace hulk::lua {
namespace {

// Lua 5.1's pseudo-indices and the results count a chunk runs with
constexpr uint32_t kGlobalsIndex = 0xFFFFFFFEu;  // LUA_GLOBALSINDEX (-10002)
constexpr uint32_t kMultiRet = 0xFFFFFFFFu;      // LUA_MULTRET (-1)
constexpr uint32_t kTopIndex = 0xFFFFFFFFu;      // -1: the value the last call left on top

// one guest page set, made once and reused: chunks, the chunk name and the probe key all live in it.
// luaL_loadbuffer copies the source and the name into Lua strings, so nothing of ours has to survive the call.
constexpr uint32_t kPageSize = 0x40000;  // 256 KiB
constexpr uint32_t kNameAt = kPageSize - 0x400;  // the chunk name goes here
constexpr uint32_t kProbeAt = kPageSize - 0x200;  // the one-time global probe's key goes here
constexpr size_t kMaxName = 0x1FF;       // a chunk name Lua reports in errors
constexpr size_t kMaxChunk = kNameAt - 1;
constexpr size_t kMaxError = 1023;       // LastError's cap, a Lua error message is a line or three

uint32_t g_page = 0;      // guest VA of the scratch page, 0 until it is made
std::string g_error;      // the message of the last failing call, "" when none

// the engine keeps its script API in a registry table keyed HC_REGISTRY rather than in _G; whether it is also in _G
// is what the one-time probe asks. A state that needs the table gets this prologue in front of every chunk, which
// moves the chunk into an environment that reads the engine's names first and _G after them. The environment lives
// in the registry, so globals a mod sets stay set between its own chunks and go away with the state.
constexpr char kRegistryEnv[] =
    "-- hulk: the engine's script API lives in the registry, not in _G (src/common/lua_bridge.cpp)\n"
    "local __hr = debug and debug.getregistry and debug.getregistry()\n"
    "local __hc = type(__hr) == 'table' and __hr.HC_REGISTRY or nil\n"
    "if type(__hc) == 'table' then\n"
    "  local __he = __hr.HULK_ENV\n"
    "  if type(__he) ~= 'table' then\n"
    "    __he = setmetatable({}, {__index = function(_, k)\n"
    "      local v = __hc[k]\n"
    "      if v ~= nil then return v end\n"
    "      return _G[k]\n"
    "    end})\n"
    "    __hr.HULK_ENV = __he\n"
    "  end\n"
    "  setfenv(1, __he)\n"
    "end\n";

// whether the state in hand exposes the API as globals: asked once per lua_State*, -1 while unanswered
uint32_t g_probed_state = 0;
bool g_global_api = true;

// a guest call of ours: plain data only, so the handler can sit in the function itself (plugins.cpp does the same)
__declspec(noinline) int GuestCall(uint32_t fn, uint32_t argc, const uint32_t* args, uint32_t& value) {
    __try {
        kernel::Thread* t = kernel::Current();
        if (!t || !t->ctx || !mem::IsCommitted(fn)) return -1;
        value = kernel::CallGuest(*t->ctx, fn, args, argc);
        return 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
}

// lua_tonumber returns a lua_Number the x86 way, on the x87 stack (generated code, x87_push), so it is read back
// from the context and popped: a wedged stack top would poison every later guest call.
float PopNumber() {
    kernel::Thread* t = kernel::Current();
    if (!t || !t->ctx) return 0;
    X86Context& ctx = *t->ctx;
    const float value = float(ctx.st[ctx.top]);
    ctx.top = (ctx.top + 1) & 7;
    return value;
}

void Reset(uint32_t L) {
    uint32_t value = 0;
    const uint32_t args[2] = {L, 0};
    GuestCall(addr::kLuaSetTop, 2, args, value);
}

int Depth(uint32_t L) {
    uint32_t value = 0;
    const uint32_t args[1] = {L};
    return GuestCall(addr::kLuaGetTop, 1, args, value) == 0 ? int(value) : -1;
}

// reads the value on top of the stack as a string, without changing what is on the stack
bool TopString(uint32_t L, std::string& out) {
    uint32_t p = 0;
    const uint32_t args[2] = {L, kTopIndex};
    if (GuestCall(addr::kLuaToString, 2, args, p) != 0 || !p) return false;
    const uint8_t* base = mem::Base();
    for (size_t n = 0; n < kMaxError; n++) {
        if (!mem::IsCommitted(p + uint32_t(n))) break;
        const char c = char(base[p + n]);
        if (!c) break;
        out.push_back(c);
    }
    return true;
}

void CaptureError(uint32_t L) {
    g_error.clear();
    if (!TopString(L, g_error)) g_error = "the game's Lua VM gave no error message";
}

// the page, made on first use: one allocation off the guest's user space, never a hard-coded address
bool Scratch(uint32_t& base) {
    if (g_page) {
        base = g_page;
        return true;
    }
    if (!mem::Base()) return false;
    uint32_t a = 0, size = kPageSize;
    if (mem::AllocateVirtual(a, size, mem::kMemReserve | mem::kMemCommit, 4 /* PAGE_READWRITE */) != 0 || !a) {
        LOGW("lua: no guest page for a chunk, the Lua bridge stays off");
        return false;
    }
    g_page = a;
    LOGI("lua: chunk scratch page at %08X", g_page);
    base = a;
    return true;
}

// whether the engine's API is in _G or only in the registry table, asked once for each state
bool GlobalApi(uint32_t L, uint32_t base) {
    if (g_probed_state == L) return g_global_api;
    g_probed_state = L;
    g_global_api = true;  // a probe that cannot answer leaves the chunks on _G, where they work without the table
    uint8_t* page = mem::Base() + base;
    constexpr char kKey[] = "am_CreateActor";
    std::memcpy(page + kProbeAt, kKey, sizeof kKey);
    uint32_t value = 0;
    const uint32_t field[3] = {L, kGlobalsIndex, base + kProbeAt};
    if (GuestCall(addr::kLuaGetField, 3, field, value) != 0) return g_global_api;
    std::string type;
    const bool read = TopString(L, type);
    Reset(L);
    // lua_tostring gives null for nil and a table, "function: 00000000" for a function
    g_global_api = read && type.rfind("function", 0) == 0;
    return g_global_api;
}

int Stage(uint32_t L, uint32_t src, size_t len, uint32_t name, int& status) {
    uint32_t value = 0;
    const uint32_t load[4] = {L, src, uint32_t(len), name};
    if (GuestCall(addr::kLuaLoadBuffer, 4, load, value) != 0) return -1;
    // luaL_loadbuffer's own codes (LUA_ERRSYNTAX is 3 in Lua 5.1) are reported as this API's 1: did not compile
    status = value ? 1 : 0;
    if (status) return 0;  // a compile error: its message is on top of the stack
    const uint32_t call[4] = {L, 0, kMultiRet, 0};
    if (GuestCall(addr::kLuaPcall, 4, call, value) != 0) return -1;
    // and lua_pcall's (LUA_ERRRUN 2, LUA_ERRMEM 4, LUA_ERRERR 5) as 2: the chunk raised
    status = value ? 2 : 0;
    return 0;
}

// copies a chunk and its name into the page; 'prologue' puts the registry environment in front of it
bool Write(uint32_t base, const char* prologue, const char* src, size_t len, const char* name, size_t& total,
           uint32_t& name_va) {
    uint8_t* page = mem::Base() + base;
    const size_t head = prologue ? std::strlen(prologue) : 0;
    if (head + len > kMaxChunk) {
        g_error = "the chunk is larger than the bridge's page";
        return false;
    }
    if (head) std::memcpy(page, prologue, head);
    std::memcpy(page + head, src, len);
    page[head + len] = 0;
    total = head + len;
    // the name is copied into a Lua string by luaL_loadbuffer as well
    size_t n = name ? std::strlen(name) : 0;
    if (n > kMaxName) n = kMaxName;
    if (n) std::memcpy(page + kNameAt, name, n);
    page[kNameAt + n] = 0;
    name_va = base + kNameAt;
    return true;
}

// runs a chunk on the state and reports it the way the callers' status codes expect. With 'results' the top of
// the stack is read before it is cleared: a string for Text, a number for Number.
enum class Want { Nothing, Text, Number };

int Execute(const char* src, size_t len, const char* name, Want want, std::string* text, float* number) {
    g_error.clear();
    if (!src) {
        g_error = "no chunk";
        return -1;
    }
    const uint32_t L = State();
    if (!L) {
        g_error = "the game has no Lua state";
        return -1;
    }
    uint32_t base = 0;
    if (!Scratch(base)) {
        g_error = "no guest page for a chunk";
        return -1;
    }
    const bool registry = !GlobalApi(L, base);
    const int depth = Depth(L);
    if (depth > 0) LOG_FIRST(4, "lua: the VM already has %d values on its stack, running a chunk anyway", depth);
    size_t total = 0;
    uint32_t name_va = 0;
    if (!Write(base, registry ? kRegistryEnv : nullptr, src, len, name, total, name_va)) return -1;
    int status = 0;
    if (Stage(L, base, total, name_va, status) != 0) {
        g_error = "the game's Lua VM faulted";
        Reset(L);
        return -1;
    }
    if (status != 0) {
        CaptureError(L);
        Reset(L);
        return status;
    }
    if (want == Want::Number) {
        std::string shown;
        // a nil (or table, or function) is not a number: lua_tostring answers null for those, and lua_tonumber
        // would turn them into 0 as if it were one
        if (!TopString(L, shown)) {
            Reset(L);
            g_error = "the expression is not a number";
            return 0;
        }
        uint32_t value = 0;
        const uint32_t args[2] = {L, kTopIndex};
        if (GuestCall(addr::kLuaToNumber, 2, args, value) != 0) {
            Reset(L);
            g_error = "the game's Lua VM faulted";
            return -1;
        }
        if (number) *number = PopNumber();
    } else if (want == Want::Text) {
        if (!TopString(L, *text)) {
            Reset(L);
            g_error = "the expression is not a string";
            return 0;
        }
    }
    Reset(L);
    return status;
}

}  // namespace

uint32_t State() {
    if (!mem::Base() || !mem::IsCommitted(addr::kScriptHandler)) return 0;
    const uint32_t handler = kernel::R32(addr::kScriptHandler);
    if (!handler || !mem::IsCommitted(handler + addr::kScriptHandlerState)) return 0;
    return kernel::R32(handler + addr::kScriptHandlerState);
}

bool Ready() { return State() != 0; }

int Run(const char* src, size_t len, const char* name) { return Execute(src, len, name, Want::Nothing, nullptr, nullptr); }

int Run(const char* src, const char* name) { return Run(src, src ? std::strlen(src) : 0, name); }

bool GetNumber(const char* expr, float* out) {
    if (!expr || !out) return false;
    std::string chunk = "return tonumber(";
    chunk += expr;
    chunk += ")";
    float value = 0;
    const int status = Execute(chunk.c_str(), chunk.size(), "@hulk/get", Want::Number, nullptr, &value);
    if (status != 0 || !g_error.empty()) return false;
    *out = value;
    return true;
}

bool GetString(const char* expr, char* buf, int cap) {
    if (!expr || !buf || cap <= 0) return false;
    buf[0] = 0;
    std::string chunk = "return (";
    chunk += expr;
    chunk += ")";
    std::string text;
    const int status = Execute(chunk.c_str(), chunk.size(), "@hulk/get", Want::Text, &text, nullptr);
    if (status != 0 || g_error.size()) return false;
    const size_t n = text.size() < size_t(cap - 1) ? text.size() : size_t(cap - 1);
    std::memcpy(buf, text.data(), n);
    buf[n] = 0;
    return true;
}

const char* LastError() { return g_error.c_str(); }

}  // namespace hulk::lua
