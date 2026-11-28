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
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"

namespace hulk {
namespace {

namespace fs = std::filesystem;

// folders a mod ends up wrapped in: an assets copy, or the Mod Tool's Game Files output dragged in whole
constexpr std::string_view kWrappedIn[] = {"assets\\", "game files\\"};

struct ModFile {
    fs::path host;
    uint64_t size;
    std::string mod;   // the folder it came from
    bool used = false;  // the game has this file: marked matching the archives here and as the game opens files
};
std::map<std::string, ModFile> g_files;  // by path, lower case with backslashes; set once before the game starts
std::map<std::string, std::string> g_additions;  // a mod file's path -> the archive's file name that adds it

std::string Key(std::string_view path) {
    std::string k(path);
    for (char& c : k) c = c == '/' ? '\\' : char(std::tolower(uint8_t(c)));
    return k;
}

// the path under the game's d: drive: d:\art\x and the device form \??\d:\art\x both give art\x
std::optional<std::string_view> DriveRel(std::string_view path) {
    if (path.size() >= 4 && path.compare(0, 4, "\\??\\") == 0) path.remove_prefix(4);
    if (path.size() > 3 && std::tolower(uint8_t(path[0])) == 'd' && path[1] == ':' &&
        (path[2] == '\\' || path[2] == '/'))
        return path.substr(3);
    return std::nullopt;
}

// the assets folder: beside the exe in a release, above it in the build output
fs::path AssetsDir() {
    fs::path dir = app::ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

// the mods folder beside it
fs::path ModsDir() {
    fs::path assets = AssetsDir();
    return assets.empty() ? assets : assets.parent_path() / "mods";
}

void WriteReadme(const fs::path& dir) {
    std::error_code ec;
    if (fs::exists(dir / "README.txt", ec)) return;
    std::ofstream out(dir / "README.txt", std::ios::binary);
    out << "Mods for Hulk Recompiled\r\n"
           "\r\n"
           "Each folder in here is one mod. A file inside a mod's folder replaces the game's file with the same\r\n"
           "path, or is added to an archive when the game has no such file.\r\n"
           "\r\n"
           "  mods\\My Mod\\art\\copyright-en.p3d      replaces art\\copyright-en.p3d inside art.rcf\r\n"
           "  mods\\My Mod\\frontend\\fe_real.txt      replaces the main menu script inside misc.rcf\r\n"
           "  mods\\My Mod\\misc.rcf                  replaces the whole misc.rcf archive\r\n"
           "  mods\\My Mod\\frontend\\new_file.txt     added to misc.rcf, beside the files of its frontend folder\r\n"
           "  mods\\My Mod\\misc.rcf\\art\\new_file.p3d added to misc.rcf, at art\\new_file.p3d inside it\r\n"
           "\r\n"
           "You don't need to know which archive holds a file: give it the path it has inside the archive. To add\r\n"
           "a file nothing matches, name the archive before the path: mods\\My Mod\\misc.rcf\\art\\new.p3d goes into\r\n"
           "misc.rcf at art\\new.p3d. A file whose top-level folder (frontend\\, art\\) matches one only that\r\n"
           "archive has is added there without naming it.\r\n"
           "Hulk Mod Tool.exe (in the game folder) extracts every file from your game so you can see the paths.\r\n"
           "\r\n"
           "- Mods load in name order. When two mods replace the same file, the later name wins.\r\n"
           "- Put _ at the start of a mod's folder name to turn it off.\r\n"
           "- Your game files are never changed. Remove a mod's folder and the game is back to normal.\r\n"
           "- The game's .lua scripts are compiled Lua 5.1 with 32-bit float numbers.\r\n"
           "- A mod wrapped in assets\\ or Game Files\\ (as the Mod Tool writes them) works as it is.\r\n"
           "- logs\\hulk.log lists each mod file as the game takes it and warns about ones nothing matched: if a\r\n"
           "  change does not show up in game, look there.\r\n";
}

// the host file's bytes at, zeros past its end
void ReadHost(const fs::path& path, uint64_t at, uint8_t* dst, size_t n) {
    std::memset(dst, 0, n);
    FILE* fp = _wfopen(path.c_str(), L"rb");
    if (!fp) {
        // its reads come back as zeros, which is easy to miss: say so, once for each file
        static std::mutex mutex;
        static std::set<std::string> warned;
        std::lock_guard lock(mutex);
        std::string key = Key(path.string());
        if (warned.insert(key).second) LOGW("mods: cannot open %s, its reads come back as zeros", key.c_str());
        return;
    }
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

// the archive's files the mods replace or add, put past its end
std::optional<Stage> PatchArchive(const std::string& path, uint64_t size, const ReadFn& read) {
    std::optional<Rcf> rcf = ReadRcf(size, read);
    if (!rcf) return std::nullopt;
    const std::string archive = Key(fs::path(path).filename().string());
    struct Placed {
        uint64_t at;
        const ModFile* file;
    };
    std::vector<Placed> placed;

    // files the mods add to this archive: under its name in a mod's folder, or beside files it has
    std::vector<std::pair<std::string, ModFile*>> adding;  // the path inside the archive, the file
    for (auto& [rel, m] : g_files) {
        std::string under = archive + '\\';
        std::string in;
        if (rel.size() > under.size() && rel.compare(0, under.size(), under) == 0) in = rel.substr(under.size());
        else {
            auto it = g_additions.find(rel);
            if (it == g_additions.end() || it->second != archive) continue;
            in = rel;
        }
        if (rcf->Find(in)) continue;  // a file the archive already has is replaced below, not added
        adding.push_back({std::move(in), &m});
    }

    // the files of this archive the mods replace, in the archive's order
    std::vector<ModFile*> replacing;
    std::vector<uint32_t> was;  // each replaced file's size as the archive has it
    for (const Rcf::File& f : rcf->files) {
        auto it = g_files.find(Key(f.name));
        if (it == g_files.end()) it = g_files.find(archive + '\\' + Key(f.name));
        replacing.push_back(it == g_files.end() ? nullptr : &it->second);
        was.push_back(it == g_files.end() ? 0 : rcf->Size(f.entry));
    }

    if (!adding.empty()) {
        // everything the archive grows by, with the files in on their sectors: past 4 GB the directory's
        // offsets cannot reach, so none of the additions go in
        uint64_t grown = 8 + (rcf->dir.size() / 12 + adding.size()) * 12;
        for (const Rcf::File& f : rcf->files) grown += 16 + f.name.size() + 4;
        for (const auto& a : adding) grown += 16 + a.first.size() + 4;
        for (const ModFile* m : replacing) if (m) grown += AlignRcf(m->size);
        for (const auto& a : adding) grown += AlignRcf(a.second->size);
        if (AlignRcf(AlignRcf(size) + grown) > 0xFFFFFFFFull) {
            LOGW("mods: %s's added files would not fit in 4 GB, none added", path.c_str());
            adding.clear();
        }
    }

    if (adding.empty()) {
        uint64_t end = AlignRcf(size);
        size_t i = 0;
        for (const Rcf::File& f : rcf->files) {
            ModFile* m = replacing[i++];
            if (!m) continue;
            if (end + m->size > 0xFFFFFFFFull) {
                LOGW("mods: %s is too big to fit in %s, left out", f.name.c_str(), path.c_str());
                continue;
            }
            LOGR("mods: %s in %s from %s (%llu bytes, was %u)", f.name.c_str(), path.c_str(), m->mod.c_str(),
                 (unsigned long long)m->size, was[i - 1]);
            m->used = true;
            rcf->Move(f.entry, uint32_t(end), uint32_t(m->size));
            placed.push_back({end, m});
            end = AlignRcf(end + m->size);
        }
        if (placed.empty()) return std::nullopt;
        uint64_t total = placed.back().at + placed.back().file->size;
        auto dir = std::make_shared<std::vector<uint8_t>>(std::move(rcf->dir));
        uint64_t dir_at = rcf->dir_at;
        return Stage{total, [dir, dir_at, placed](uint64_t at, uint8_t* dst, size_t n) {
                         Overlay(at, dst, n, dir_at, dir->data(), dir->size());
                         for (const Placed& p : placed) {
                             uint64_t lo = std::max(at, p.at), hi = std::min<uint64_t>(at + n, p.at + p.file->size);
                             if (lo < hi) ReadHost(p.file->host, lo - p.at, dst + (lo - at), size_t(hi - lo));
                         }
                     }};
    }

    // the added files go in with the replaced ones: WriteRcf puts them past the archive's end, each on its
    // own sector, and builds the directory, the names and the header that point at them
    std::vector<std::pair<uint32_t, uint32_t>> entries;  // entry, the size its file takes; the replaced
    size_t i = 0;                                        // ones in the archive's order, then the added ones
    for (const Rcf::File& f : rcf->files)
        if (ModFile* m = replacing[i++]; m) entries.push_back({f.entry, uint32_t(m->size)});
    for (const auto& a : adding) entries.push_back({rcf->Add(a.first, uint32_t(a.second->size)), uint32_t(a.second->size)});
    RcfWritten written = WriteRcf(*rcf, entries, size);

    uint64_t end = written.data;
    i = 0;
    for (const Rcf::File& f : rcf->files) {
        ModFile* m = replacing[i];
        uint32_t before = was[i++];
        if (!m) continue;
        LOGR("mods: %s in %s from %s (%llu bytes, was %u)", f.name.c_str(), path.c_str(), m->mod.c_str(),
             (unsigned long long)m->size, before);
        m->used = true;
        placed.push_back({end, m});
        end = AlignRcf(end + m->size);
    }
    for (const auto& a : adding) {
        LOGR("mods: %s added to %s from %s (%llu bytes)", a.first.c_str(), path.c_str(), a.second->mod.c_str(),
             (unsigned long long)a.second->size);
        a.second->used = true;
        placed.push_back({end, a.second});
        end = AlignRcf(end + a.second->size);
    }
    auto head = std::make_shared<std::vector<uint8_t>>(std::move(written.header));
    auto dir = std::make_shared<std::vector<uint8_t>>(std::move(written.dir));
    auto names = std::make_shared<std::vector<uint8_t>>(std::move(written.names));
    const uint64_t dir_at = written.at, names_at = written.at + dir->size();
    return Stage{written.size, [head, dir, names, dir_at, names_at, placed](uint64_t at, uint8_t* dst, size_t n) {
                     Overlay(at, dst, n, 0, head->data(), head->size());
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

// what the game has for the mods to stand in for: each archive's files, read from the extracted game, else from the
// disc image, the way the game and the mod tool find it. marks the mod files the game will take - replacing a file of
// an archive or of the disc's own, or added to an archive - and warns about the rest: a mod file that matches nothing
// is the usual reason a mod seems to do nothing
void LogUnmatched() {
    fs::path assets = AssetsDir();
    std::error_code ec;
    struct Archive {
        std::string name;  // the file's name as the game opens it: misc.rcf
        Rcf rcf;
    };
    std::vector<Archive> archives;
    std::unique_ptr<kernel::Disc> disc;  // the game from an image, when it is not extracted beside it
    if (fs::exists(assets / "default.xbe", ec)) {
        for (auto& e : fs::directory_iterator(assets, ec)) {
            if (!e.is_regular_file(ec) || !EndsWithNoCase(e.path().extension().string(), ".rcf")) continue;
            auto fp = std::shared_ptr<FILE>(_wfopen(e.path().c_str(), L"rb"), [](FILE* f) { if (f) std::fclose(f); });
            if (!fp) continue;
            uint64_t size = fs::file_size(e.path(), ec);
            if (ec) continue;
            ReadFn read = [fp](uint64_t at, void* dst, size_t n) -> size_t {
                _fseeki64(fp.get(), int64_t(at), SEEK_SET);
                return std::fread(dst, 1, n, fp.get());
            };
            if (auto rcf = ReadRcf(size, read))
                archives.push_back({Key(e.path().filename().string()), std::move(*rcf)});
        }
    } else {
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec))
            if (e.is_regular_file(ec) && EndsWithNoCase(e.path().extension().string(), ".iso")) isos.push_back(e.path());
        std::sort(isos.begin(), isos.end());
        for (const fs::path& iso : isos) {
            auto d = std::make_unique<kernel::Disc>();
            std::string error;
            if (d->Open(iso, error)) {
                disc = std::move(d);
                break;
            }
        }
        if (!disc) return;  // no game here at all: the loader tells the player so itself
        const kernel::Disc::Entry* root = disc->Find(std::vector<std::string>{});
        if (!root) return;
        for (const kernel::Disc::Entry& e : root->children) {
            if (e.dir || !EndsWithNoCase(e.name, ".rcf")) continue;
            kernel::Disc* d = disc.get();
            ReadFn read = [d, &e](uint64_t at, void* dst, size_t n) -> size_t {
                size_t total = 0;
                while (total < n) {
                    uint32_t chunk = uint32_t(std::min<size_t>(n - total, 0x10000000));
                    uint32_t got = d->Read(e, at + total, static_cast<uint8_t*>(dst) + total, chunk);
                    total += got;
                    if (got < chunk) break;
                }
                return total;
            };
            if (auto rcf = ReadRcf(e.size, read)) archives.push_back({Key(e.name), std::move(*rcf)});
        }
    }
    // files the game has: each archive's, by their path in the archive or under the archive's name for the
    // files a mod means for it
    for (const Archive& a : archives)
        for (const Rcf::File& f : a.rcf.files) {
            auto it = g_files.find(Key(f.name));
            if (it == g_files.end()) it = g_files.find(a.name + '\\' + Key(f.name));
            if (it != g_files.end()) it->second.used = true;
        }
    for (auto& [rel, m] : g_files) {
        if (m.used) continue;
        if (disc ? disc->Find(rel) != nullptr : fs::exists(assets / fs::path(rel).make_preferred(), ec))
            m.used = true;
    }
    // what is left, the archives add: a file under an archive's name in the mod's folder goes into it, and
    // a file whose top-level folder matches one only that archive has goes in beside its files
    for (auto& [rel, m] : g_files) {
        if (m.used) continue;
        std::string where;
        size_t slash = rel.find('\\');
        if (slash == std::string::npos) {
        } else if (EndsWithNoCase(rel.substr(0, slash), ".rcf")) {
            for (const Archive& a : archives)
                if (a.name == rel.substr(0, slash)) where = a.name;
        } else {
            const Archive* found = nullptr;
            bool ambiguous = false;
            for (const Archive& a : archives) {
                bool has = false;
                for (const Rcf::File& f : a.rcf.files)
                    has |= f.name.size() > slash + 1 &&
                           SamePath(std::string_view(f.name).substr(0, slash + 1), rel.substr(0, slash + 1));
                if (!has) continue;
                if (found) ambiguous = true;  // more than one archive has the folder: the mod names the
                found = &a;                   // archive to be sure
            }
            if (found && !ambiguous) where = found->name;
        }
        if (where.empty()) continue;
        g_additions.emplace(rel, where);
        m.used = true;
        LOGR("mods: %s's %s will be added to %s", m.mod.c_str(), rel.c_str(), where.c_str());
    }
    for (auto& [rel, m] : g_files) {
        if (m.used) continue;
        LOGW("mods: %s's %s matches nothing the game has, it will not be used", m.mod.c_str(), rel.c_str());
    }
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
            if (ec) continue;
            uint64_t size = fs::file_size(it->path(), ec);
            if (ec) continue;
            // a mod wrapped in an assets copy or the Mod Tool's Game Files output still works: drop that folder
            for (std::string_view w : kWrappedIn) {
                if (rel.size() > w.size() && rel.compare(0, w.size(), w) == 0) {
                    std::string wrapped = rel;
                    rel.erase(0, w.size());
                    LOGR("mods: %s: %s is wrapped in its %.*s folder, using it as %s", name.c_str(), wrapped.c_str(),
                         int(w.size() - 1), w.data(), rel.c_str());
                    break;
                }
            }
            auto old = g_files.find(rel);
            if (old != g_files.end()) LOGR("mods: %s replaces %s's %s", name.c_str(), old->second.mod.c_str(), rel.c_str());
            g_files[rel] = {it->path(), size, name};
            count++;
        }
        LOGR("mods: %s, %zu files", name.c_str(), count);
    }
    if (!mods.empty()) {
        LOGR("mods: %zu mods in %s", mods.size(), dir.string().c_str());
        LogUnmatched();
    }
}

std::shared_ptr<app::FileView> ModFileView(const std::string& path, uint64_t size,
                                           const std::function<size_t(uint64_t, void*, size_t)>& read) {
    auto stages = std::make_shared<std::vector<Stage>>();
    ReadFn own = read;
    std::optional<std::string_view> rel = g_files.empty() ? std::nullopt : DriveRel(path);
    if (rel) {
        // the whole file
        auto it = g_files.find(Key(*rel));
        if (it != g_files.end()) {
            const ModFile& m = it->second;
            it->second.used = true;
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
