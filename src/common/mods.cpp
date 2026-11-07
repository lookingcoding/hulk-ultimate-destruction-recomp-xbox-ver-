#include "common/mods.h"
#include "common/display_menu.h"
#include "common/rcf.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <vector>
#include "app/app.h"
#include "core/log.h"

namespace hulk {
namespace {

namespace fs = std::filesystem;

struct ModFile {
    fs::path host;
    uint64_t size;
    std::string mod;  // the folder it came from
};
std::map<std::string, ModFile> g_files;  // by path, lower case with backslashes; set once before the game starts

std::string Key(std::string_view path) {
    std::string k(path);
    for (char& c : k) c = c == '/' ? '\\' : char(std::tolower(uint8_t(c)));
    return k;
}

// the folder holding assets: beside the exe in a release, above it in the build output
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

void WriteReadme(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / "README.txt", ec)) return;
    std::ofstream out(dir / "README.txt", std::ios::binary);
    out << "Mods for Hulk Recompiled\r\n"
           "\r\n"
           "Each folder in here is one mod. A file inside a mod's folder replaces the game's file with the same path.\r\n"
           "\r\n"
           "  mods\\My Mod\\art\\copyright-en.p3d      replaces art\\copyright-en.p3d inside art.rcf\r\n"
           "  mods\\My Mod\\frontend\\fe_real.txt      replaces the main menu script inside misc.rcf\r\n"
           "  mods\\My Mod\\misc.rcf                  replaces the whole misc.rcf archive\r\n"
           "\r\n"
           "You don't need to know which archive holds a file: give it the path it has inside the archive.\r\n"
           "Hulk Mod Tool.exe (in the game folder) extracts every file from your game so you can see the paths.\r\n"
           "\r\n"
           "- Mods load in name order. When two mods replace the same file, the later name wins.\r\n"
           "- Put _ at the start of a mod's folder name to turn it off.\r\n"
           "- Your game files are never changed. Remove a mod's folder and the game is back to normal.\r\n"
           "- Mods can only replace files the game already has, not add new ones.\r\n"
           "- The game's .lua scripts are compiled Lua 5.1 with 32-bit float numbers.\r\n"
           "- logs\\hulk.log lists every mod file the game picked up.\r\n";
}

// the host file's bytes at, zeros past its end
void ReadHost(const fs::path& path, uint64_t at, uint8_t* dst, size_t n) {
    std::memset(dst, 0, n);
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) return;
    _fseeki64(fp, int64_t(at), SEEK_SET);
    std::fread(dst, 1, n, fp);
    std::fclose(fp);
}

// a change to what the game reads of a file: given the bytes as the stages before it left them, from at
struct Stage {
    uint64_t size;  // the file's size after it
    std::function<void(uint64_t at, uint8_t* dst, size_t n)> apply;
};

// the bytes the stages make, as a read for whoever builds the next stage
ReadFn Through(const ReadFn& own, uint64_t own_size, std::shared_ptr<std::vector<Stage>> stages) {
    return [own, own_size, stages](uint64_t at, void* out, size_t n) -> size_t {
        uint64_t size = stages->empty() ? own_size : stages->back().size;
        if (at >= size) return 0;
        n = size_t(std::min<uint64_t>(n, size - at));
        auto* dst = static_cast<uint8_t*>(out);
        std::memset(dst, 0, n);
        if (at < own_size) own(at, dst, size_t(std::min<uint64_t>(n, own_size - at)));
        for (const Stage& s : *stages) s.apply(at, dst, n);
        return n;
    };
}

// overlays src, placed at from in the file, on dst, which holds the file from at
void Overlay(uint64_t at, uint8_t* dst, size_t n, uint64_t from, const uint8_t* src, size_t len) {
    uint64_t lo = std::max(at, from), hi = std::min<uint64_t>(at + n, from + len);
    if (lo < hi) std::memcpy(dst + (lo - at), src + (lo - from), size_t(hi - lo));
}

// the archive's files the mods replace, put past its end
std::optional<Stage> PatchArchive(const std::string& path, uint64_t size, const ReadFn& read) {
    std::optional<Rcf> rcf = ReadRcf(size, read);
    if (!rcf) return std::nullopt;
    struct Placed {
        uint64_t at;
        const ModFile* file;
    };
    std::vector<Placed> placed;
    uint64_t end = AlignRcf(size);
    for (const Rcf::File& f : rcf->files) {
        auto it = g_files.find(Key(f.name));
        if (it == g_files.end()) continue;
        const ModFile& m = it->second;
        if (end + m.size > 0xFFFFFFFFull) {
            LOGW("mods: %s is too big to fit in %s, left out", f.name.c_str(), path.c_str());
            continue;
        }
        LOGR("mods: %s in %s from %s (%llu bytes, was %u)", f.name.c_str(), path.c_str(), m.mod.c_str(),
             (unsigned long long)m.size, rcf->Size(f.entry));
        rcf->Move(f.entry, uint32_t(end), uint32_t(m.size));
        placed.push_back({end, &m});
        end = AlignRcf(end + m.size);
    }
    if (placed.empty()) return std::nullopt;
    uint64_t total = placed.back().at + placed.back().file->size;
    auto dir = std::make_shared<std::vector<uint8_t>>(std::move(rcf->dir));
    auto names = std::make_shared<std::vector<uint8_t>>(std::move(rcf->names));
    uint64_t dir_at = rcf->dir_at;
    uint64_t names_at = rcf->names_at;
    return Stage{total, [dir, dir_at, names, names_at, placed](uint64_t at, uint8_t* dst, size_t n) {
                     Overlay(at, dst, n, dir_at, dir->data(), dir->size());
                     Overlay(at, dst, n, names_at, names->data(), names->size());
                     for (const Placed& p : placed) {
                         uint64_t lo = std::max(at, p.at), hi = std::min<uint64_t>(at + n, p.at + p.file->size);
                         if (lo < hi) ReadHost(p.file->host, lo - p.at, dst + (lo - at), size_t(hi - lo));
                     }
                 }};
}

bool EndsWithNoCase(const std::string& s, const char* suffix) {
    size_t n = std::strlen(suffix);
    return s.size() >= n && SamePath(std::string_view(s).substr(s.size() - n), suffix);
}

}  // namespace

void LoadMods() {
    fs::path dir = ModsDir();
    if (dir.empty()) return;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) fs::create_directories(dir, ec);
    WriteReadme(dir);

    std::vector<fs::path> mods;
    for (auto& e : fs::directory_iterator(dir, ec)) {
        std::string name = e.path().filename().string();
        if (e.is_directory(ec) && !name.empty() && name[0] != '_' && name[0] != '.') mods.push_back(e.path());
    }
    std::sort(mods.begin(), mods.end(), [](const fs::path& a, const fs::path& b) {
        return Key(a.filename().string()) < Key(b.filename().string());
    });
    for (const fs::path& mod : mods) {
        std::string name = mod.filename().string();
        size_t count = 0;
        for (auto it = fs::recursive_directory_iterator(mod, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec || !it->is_regular_file(ec)) continue;
            std::string rel = Key(fs::relative(it->path(), mod, ec).string());
            uint64_t size = fs::file_size(it->path(), ec);
            if (ec) continue;
            auto old = g_files.find(rel);
            if (old != g_files.end()) LOGR("mods: %s replaces %s's %s", name.c_str(), old->second.mod.c_str(), rel.c_str());
            g_files[rel] = {it->path(), size, name};
            count++;
        }
        LOGR("mods: %s, %zu files", name.c_str(), count);
    }
    if (!mods.empty()) LOGR("mods: %zu mods in %s", mods.size(), dir.string().c_str());
}

std::shared_ptr<app::FileView> ModFileView(const std::string& path, uint64_t size,
                                           const std::function<size_t(uint64_t, void*, size_t)>& read) {
    auto stages = std::make_shared<std::vector<Stage>>();
    ReadFn own = read;
    if (!g_files.empty() && path.size() > 3 && std::tolower(uint8_t(path[0])) == 'd' && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/')) {
        // the whole file
        auto it = g_files.find(Key(path.substr(3)));
        if (it != g_files.end()) {
            const ModFile& m = it->second;
            LOGR("mods: %s from %s (%llu bytes)", path.c_str(), m.mod.c_str(), (unsigned long long)m.size);
            stages->push_back({m.size, [host = m.host](uint64_t at, uint8_t* dst, size_t n) { ReadHost(host, at, dst, n); }});
        }
        // files inside it
        if (EndsWithNoCase(path, ".rcf")) {
            uint64_t now = stages->empty() ? size : stages->back().size;
            if (auto patch = PatchArchive(path, now, Through(own, size, stages))) stages->push_back(std::move(*patch));
        }
    }
    // Options > Display Options, on top of whatever the mods did to misc.rcf
    uint64_t now = stages->empty() ? size : stages->back().size;
    if (auto menu = GameFileView(path, now, Through(own, size, stages)))
        stages->push_back({menu->size, menu->read});
    if (stages->empty()) return nullptr;
    auto view = std::make_shared<app::FileView>();
    view->size = stages->back().size;
    view->read = [stages](uint64_t at, uint8_t* dst, size_t n) {
        for (const Stage& s : *stages) s.apply(at, dst, n);
    };
    return view;
}

}  // namespace hulk
