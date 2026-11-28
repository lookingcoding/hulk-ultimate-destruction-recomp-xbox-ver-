#include "common/lua_mods.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "app/app.h"
#include "common/lua_bridge.h"
#include "common/replay.h"
#include "core/log.h"

namespace hulk::lua_mods {
namespace {

namespace fs = std::filesystem;

// a script that fails this many OnTick calls in a row is left out: one error is logged, the third ends it
constexpr unsigned kMaxFailures = 3;
constexpr uint64_t kMaxScript = 256 * 1024;  // a mod's script is text; the bridge's page caps it again

// Every script the scan found, in name order. The scan happens once, the first time the game has a Lua state;
// the scripts themselves are run again for every fresh state, because ScriptHandler::Init closes the old one.
struct Script {
    fs::path path;
    std::string chunk;             // the name Lua reports in errors: "@mods/My Mod/lua/main.lua"
    std::string key;               // the global its OnTick is parked in, __hulk_tick_<n>
    unsigned failures = 0;
    bool dead = false;             // OnTick failed too often: no longer called, the script's globals stay
};

std::vector<Script> g_scripts;
bool g_scanned = false;
uint32_t g_state = 0;  // the state the scripts last ran on

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

// a name or folder starting with _ or . is off, the same as it is for the file mods and the plugins
bool Off(const fs::path& path) {
    const std::string name = path.filename().string();
    return name.empty() || name[0] == '_' || name[0] == '.';
}

bool IsLua(const fs::path& path, std::error_code& ec) {
    if (!fs::is_regular_file(path, ec) || ec) return false;
    std::string ext = path.extension().string();
    for (char& c : ext) c = char(std::tolower(uint8_t(c)));
    return ext == ".lua";
}

// one folder's *.lua files, "_ off" applied
void Collect(const fs::path& dir, std::error_code& ec, std::vector<fs::path>& files) {
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (Off(e.path())) continue;
        if (IsLua(e.path(), ec)) files.push_back(e.path());
    }
}

// mods\*.lua, then every mod's mods\<Mod>\lua\*.lua, both in name order
void Scan() {
    g_scripts.clear();
    const fs::path dir = ModsDir();
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> files;
    Collect(dir, ec, files);
    for (auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_directory(ec) || Off(e.path())) continue;
        const fs::path lua = e.path() / "lua";
        if (!fs::is_directory(lua, ec)) continue;
        Collect(lua, ec, files);
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        std::string la = a.string(), lb = b.string();
        for (char& c : la) c = char(std::tolower(uint8_t(c)));
        for (char& c : lb) c = char(std::tolower(uint8_t(c)));
        return la < lb;
    });
    for (fs::path& path : files) {
        Script script;
        script.path = std::move(path);
        std::error_code rel;
        script.chunk = "@mods/" + fs::relative(script.path, dir, rel).string();
        if (rel) script.chunk = "@mods/" + script.path.filename().string();
        std::replace(script.chunk.begin(), script.chunk.end(), '\\', '/');
        script.key = "__hulk_tick_" + std::to_string(g_scripts.size());
        g_scripts.push_back(std::move(script));
    }
    if (!g_scripts.empty()) LOGR("lua mods: %zu script(s) in %s", g_scripts.size(), dir.string().c_str());
}

bool Read(const fs::path& path, std::string& out) {
    std::error_code ec;
    const uint64_t size = fs::file_size(path, ec);
    if (ec || !size || size > kMaxScript) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    // an editor's UTF-8 BOM is not Lua 5.1's business: Lua would report the first byte as an unexpected symbol
    if (out.size() >= 3 && uint8_t(out[0]) == 0xEF && uint8_t(out[1]) == 0xBB && uint8_t(out[2]) == 0xBF)
        out.erase(0, 3);
    return !out.empty();
}

// What the host's tick calls: the script's OnTick is parked where _G always reaches, whichever environment the
// chunk ran in. A plain read of OnTick would pick up an earlier mod's, so the value is remembered before the
// script runs and only a function of the script's own is registered: nothing is cleared, so a game global of
// that name stays. The three chunks are separate on purpose: a script is run exactly as it is on disk, so one
// that ends with a top-level `return` still compiles.
constexpr char kSnapshot[] = "rawset(_G, '__hulk_prev_on_tick', OnTick)\n";

std::string Register(const Script& script) {
    return "do\n"
           "  local __hulk_before = rawget(_G, '__hulk_prev_on_tick')\n"
           "  rawset(_G, '__hulk_prev_on_tick', nil)\n"
           "  local __hulk_f = OnTick\n"
           "  rawset(_G, '" + script.key + "', (type(__hulk_f) == 'function' and __hulk_f ~= __hulk_before)"
           " and __hulk_f or nil)\n"
           "end\n";
}

void Load(size_t index) {
    Script& script = g_scripts[index];
    script.failures = 0;
    script.dead = false;
    std::string source;
    if (!Read(script.path, source)) {
        LOGE("lua mods: %s is empty, too large or unreadable, left out", script.chunk.c_str());
        return;
    }
    lua::Run(kSnapshot, sizeof kSnapshot - 1, "@hulk/before");
    const int status = lua::Run(source.c_str(), source.size(), script.chunk.c_str());
    if (status != 0) {
        LOGE("lua mods: %s did not load: %s", script.chunk.c_str(), lua::LastError());
        return;
    }
    const std::string reg = Register(script);
    if (lua::Run(reg.c_str(), reg.size(), "@hulk/after") != 0)
        LOGE("lua mods: %s loaded, but its OnTick could not be registered: %s", script.chunk.c_str(),
             lua::LastError());
    char type[32] = "";
    const bool ticking = lua::GetString(("type(_G['" + script.key + "'])").c_str(), type, sizeof type) &&
                         std::strcmp(type, "function") == 0;
    LOGR("lua mods: %s loaded%s", script.chunk.c_str(), ticking ? ", OnTick each step" : "");
}

// runs one script's OnTick(dt) through pcall, in a chunk of its own so its errors are its own
void TickScript(size_t index, float dt) {
    Script& script = g_scripts[index];
    if (script.dead) return;
    char chunk[192];
    std::snprintf(chunk, sizeof chunk,
                  "local f = rawget(_G, '%s')\n"
                  "if type(f) == 'function' then return f(%.9g) end\n",
                  script.key.c_str(), double(dt));
    const int status = lua::Run(chunk, std::strlen(chunk), "@hulk/tick");
    if (status == 0) {
        script.failures = 0;
        return;
    }
    if (status < 0) return;  // no state to run on (the VM is being replaced): not the script's fault
    script.failures++;
    LOGE("lua mods: %s OnTick failed (%u of %u): %s", script.chunk.c_str(), script.failures, kMaxFailures,
         lua::LastError());
    if (script.failures < kMaxFailures) return;
    script.dead = true;
    LOGE("lua mods: %s disabled, its OnTick is no longer called", script.chunk.c_str());
}

}  // namespace

void Tick(float dt) {
    // a background test drives the game through its own hidden window: it runs no scripts of the player's
    if (hulk::BackgroundTest()) return;
    const uint32_t state = lua::State();
    if (!state) return;
    if (!g_scanned) {
        g_scanned = true;
        Scan();
    }
    // a fresh lua_State (a mission loaded, the game restarted): every script runs again on it
    if (state != g_state) {
        g_state = state;
        LOGI("lua mods: state %08X, running %zu script(s)", state, g_scripts.size());
        for (size_t i = 0; i < g_scripts.size(); i++) Load(i);
    }
    if (g_scripts.empty() || !std::isfinite(dt)) return;
    for (size_t i = 0; i < g_scripts.size(); i++) TickScript(i, dt);
}

}  // namespace hulk::lua_mods
