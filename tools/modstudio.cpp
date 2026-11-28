// Hulk Mod Studio.exe: an OpenIV-style browser for the game's archives and a manager for mods. It reads the game in
// assets the way the game and the Mod Tool do (the extracted files, else the first disc image), so nothing has to be
// extracted first: every archive's folders and files are listed straight out of its directory, and single files come
// out on demand. Replace in Mod copies a file from disk into mods\<mod>\<the path it has inside the archive>, which
// is all a mod is (src/common/mods.h). The Mod Manager tab turns mods on and off the way the game reads them, by
// renaming their folder with or without a _ in front.
// The flags after the exe name run it without a window, for tests and scripts:
//   Hulk Mod Studio.exe --list [game folder]
//   Hulk Mod Studio.exe --extract <archive> <file inside> <output file> [game folder]
//   Hulk Mod Studio.exe --replace <archive> <file inside> <source file> <mod name> [game folder]
//   Hulk Mod Studio.exe --selftest [game folder]   loads everything with the window hidden, exits 0 when it worked
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include "common/rcf.h"
#include "kernel/disc.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>

namespace fs = std::filesystem;

namespace {

constexpr int kSearchLimit = 5000;  // rows shown for one search, so the tool stays quick

// ---------------------------------------------------------------- small helpers

std::wstring Widen(std::string_view s) {  // the archives' names are ASCII
    std::wstring w;
    w.reserve(s.size());
    for (char c : s) w.push_back(wchar_t(uint8_t(c)));
    return w;
}

std::string NarrowUtf8(std::wstring_view s) {
    std::string n(WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), n.data(), int(n.size()), nullptr, nullptr);
    return n;
}

char FoldChar(char c) { return c == '/' ? '\\' : char(std::tolower(uint8_t(c))); }

std::string Folded(std::string_view s) {
    std::string f(s);
    for (char& c : f) c = FoldChar(c);
    return f;
}

bool NameLess(const std::string& a, const std::string& b) {
    size_t n = std::min(a.size(), b.size());
    for (size_t i = 0; i < n; i++)
        if (char x = FoldChar(a[i]), y = FoldChar(b[i]); x != y) return x < y;
    return a.size() < b.size();
}

std::wstring SizeText(uint64_t b) {
    wchar_t buf[64];
    if (b >= 1024 * 1024 * 1024) swprintf(buf, 64, L"%.1f GB", double(b) / (1024 * 1024 * 1024));
    else if (b >= 1024 * 1024) swprintf(buf, 64, L"%.1f MB", double(b) / (1024 * 1024));
    else if (b >= 1024) swprintf(buf, 64, L"%.1f KB", double(b) / 1024);
    else swprintf(buf, 64, L"%u bytes", unsigned(b));
    return buf;
}

// a path inside an archive the game could take: the same guard the Mod Tool applies to names
bool ValidRel(std::string_view rel) {
    return !rel.empty() && rel.find("..") == std::string_view::npos && rel[0] != '/' && rel[0] != '\\' &&
           rel.find(':') == std::string_view::npos;
}

// "art\cars\x.p3d" as a host path under a mod
fs::path RelPath(std::string_view rel) {
    std::wstring w;
    w.reserve(rel.size());
    for (char c : rel) w.push_back(c == '\\' ? L'/' : wchar_t(uint8_t(c)));
    return fs::path(w).make_preferred();
}

bool UnderFolder(const std::string& name, const std::string& prefix) {
    if (prefix.empty()) return true;
    if (name.size() <= prefix.size() || FoldChar(name[prefix.size()]) != '\\') return false;
    for (size_t i = 0; i < prefix.size(); i++)
        if (FoldChar(name[i]) != FoldChar(prefix[i])) return false;
    return true;
}

std::wstring LastComponent(const std::string& name) {
    size_t s = name.find_last_of("\\/");
    return Widen(s == std::string::npos ? name : name.substr(s + 1));
}

bool ValidModName(const std::wstring& name) {
    if (name.empty() || name.size() > 64 || name[0] == L'_' || name[0] == L'.') return false;
    if (name.front() == L' ' || name.back() == L' ' || name.back() == L'.') return false;
    for (wchar_t c : name)
        if (wcschr(L"<>:\"/\\|?*", c) || uint16_t(c) < 0x20) return false;
    return true;
}

std::wstring Trimmed(std::wstring s) {
    size_t a = s.find_first_not_of(L" \t"), b = s.find_last_not_of(L" \t");
    return a == std::wstring::npos ? std::wstring{} : s.substr(a, b - a + 1);
}

// ---------------------------------------------------------------- paths

fs::path ExeDir() {
    std::wstring p(32768, L'\0');
    p.resize(GetModuleFileNameW(nullptr, p.data(), DWORD(p.size())));
    return fs::path(p).parent_path();
}

// the assets folder: beside the exe in a release, above it in the build output
fs::path FindAssets() {
    fs::path dir = ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / L"assets", ec)) return dir / L"assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

// a folder given on the command line: the game folder, or the assets folder itself
fs::path ResolveAssets(const fs::path& arg) {
    std::error_code ec;
    if (!arg.empty()) {
        if (fs::is_directory(arg / L"assets", ec)) return arg / L"assets";
        return arg;
    }
    return FindAssets();
}

fs::path FindLog(const fs::path& root) {
    std::error_code ec;
    for (const wchar_t* rel : {L"game\\logs\\hulk.log", L"logs\\hulk.log"})
        if (!root.empty() && fs::exists(root / rel, ec)) return root / rel;
    return {};
}

// ---------------------------------------------------------------- the game's archives

std::atomic<bool> g_abort{false};

struct Archive {
    std::string name;                // art.rcf: what the game opens at d:\art.rcf
    uint64_t size = 0;
    hulk::ReadFn read;
    hulk::Rcf rcf;                   // its directory, when it is a cement library
    std::vector<std::string> folded; // the names folded for searching, in the files' order
    bool is_rcf = false;
};

struct LoadResult {
    std::vector<Archive> archives;
    std::unique_ptr<kernel::Disc> disc;  // the disc image the archives came from, when they did
    fs::path assets;                     // the assets folder used
    fs::path root;                       // the folder around it, which holds mods
    size_t files = 0;
    std::string error;                   // when no game was found
};

// ---------------------------------------------------------------- work the GUI and the flags share

bool WriteOut(const fs::path& dest, const hulk::ReadFn& read, uint64_t size, std::wstring& err,
              const std::atomic<bool>* abort) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::remove(dest, ec);  // write fresh: an old file with more bytes would keep its tail
    FILE* out = _wfopen(dest.c_str(), L"wb");
    if (!out) {
        err = L"cannot write " + dest.wstring();
        return false;
    }
    std::vector<uint8_t> buf(4 << 20);
    uint64_t at = 0;
    bool ok = true;
    while (at < size) {
        if (abort && *abort) {
            ok = false;
            break;
        }
        size_t n = size_t(std::min<uint64_t>(buf.size(), size - at));
        size_t got = read(at, buf.data(), n);
        if (!got) {
            err = L"the archive could not be read";
            ok = false;
            break;
        }
        if (std::fwrite(buf.data(), 1, got, out) != got) {
            err = L"cannot write " + dest.wstring();
            ok = false;
            break;
        }
        at += got;
    }
    if (std::fclose(out) != 0 && ok) {
        err = L"cannot write " + dest.wstring();
        ok = false;
    }
    if (!ok) fs::remove(dest, ec);  // a partial file would look like a finished one
    return ok;
}

// one file out of an archive
bool ExtractFile(const Archive& a, uint32_t fi, const fs::path& dest, std::wstring& err, const std::atomic<bool>* abort) {
    uint32_t entry = a.rcf.files[fi].entry;
    uint64_t size = a.rcf.Size(entry);
    uint32_t at = a.rcf.Offset(entry);
    return WriteOut(dest, [&a, at](uint64_t p, void* dst, size_t n) { return a.read(at + p, dst, n); }, size, err, abort);
}

// the whole archive file, as it is on the disc
bool ExtractWholeArchive(const Archive& a, const fs::path& dest, std::wstring& err, const std::atomic<bool>* abort) {
    return WriteOut(dest, a.read, a.size, err, abort);
}

// every file under a folder of an archive, into a folder on disk; files already there are left alone
void ExtractFolder(const Archive& a, const std::string& prefix, const fs::path& dir, size_t& written, size_t& kept,
                   size_t& failed, std::wstring& err, const std::atomic<bool>* abort) {
    for (size_t i = 0; i < a.rcf.files.size(); i++) {
        if (abort && *abort) return;
        const std::string& name = a.rcf.files[i].name;
        if (!UnderFolder(name, prefix)) continue;
        if (!ValidRel(name)) {
            failed++;
            continue;
        }
        fs::path dest = dir / RelPath(name);
        std::error_code ec;
        if (fs::exists(dest, ec)) {
            kept++;
            continue;
        }
        if (ExtractFile(a, uint32_t(i), dest, err, abort)) written++;
        else failed++;
    }
}

// a file from disk into mods\<mod>\<the path it has inside the archive>
bool ReplaceInMod(const fs::path& root, const std::string& internal, const fs::path& src, const std::wstring& mod,
                  std::wstring& err) {
    if (!ValidRel(internal)) {
        err = L"that name cannot be used as a mod path";
        return false;
    }
    fs::path dest = root / L"mods" / mod / RelPath(internal);
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        err = L"cannot copy into " + dest.wstring() + L": " + Widen(ec.message());
        return false;
    }
    return true;
}

struct ModRow {
    std::wstring name;
    bool enabled = false;
    size_t files = 0;
    uint64_t bytes = 0;
};

std::vector<ModRow> ScanMods(const fs::path& root) {
    std::vector<ModRow> rows;
    std::error_code ec;
    fs::path mods = root / L"mods";
    if (root.empty() || !fs::is_directory(mods, ec)) return rows;
    for (auto& e : fs::directory_iterator(mods, ec)) {
        std::wstring name = e.path().filename().wstring();
        if (!e.is_directory(ec) || name.empty() || name[0] == L'.') continue;
        ModRow r{name, name[0] != L'_', 0, 0};
        for (auto it = fs::recursive_directory_iterator(e.path(), ec); it != fs::recursive_directory_iterator();
             it.increment(ec))
            if (!ec && it->is_regular_file(ec)) {
                r.files++;
                uint64_t s = fs::file_size(it->path(), ec);
                if (!ec) r.bytes += s;
            }
        rows.push_back(std::move(r));
    }
    std::sort(rows.begin(), rows.end(),
              [](const ModRow& x, const ModRow& y) { return NameLess(NarrowUtf8(x.name), NarrowUtf8(y.name)); });
    return rows;
}

void LoadArchive(const std::string& name, uint64_t size, const hulk::ReadFn& read, LoadResult& out) {
    Archive a;
    a.name = name;
    a.size = size;
    a.read = read;
    if (auto rcf = hulk::ReadRcf(size, read)) {
        a.rcf = std::move(*rcf);
        a.is_rcf = true;
        a.folded.reserve(a.rcf.files.size());
        for (hulk::Rcf::File& f : a.rcf.files) a.folded.push_back(Folded(f.name));
        out.files += a.rcf.files.size();
    }
    out.archives.push_back(std::move(a));
}

void LoadGameInto(const fs::path& assets, LoadResult& out, const std::atomic<bool>* abort) {
    std::error_code ec;
    if (assets.empty() || !fs::is_directory(assets, ec)) {
        out.error = "no assets folder found beside or above this tool";
        return;
    }
    out.assets = assets;
    out.root = assets.parent_path();

    if (fs::exists(assets / "default.xbe", ec)) {  // the extracted game
        for (auto& e : fs::directory_iterator(assets, ec)) {
            if (abort && *abort) return;
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (!e.is_regular_file(ec) || ext != L".rcf") continue;
            auto fp = std::shared_ptr<FILE>(_wfopen(e.path().c_str(), L"rb"), [](FILE* f) { if (f) std::fclose(f); });
            if (!fp) continue;
            uint64_t size = fs::file_size(e.path(), ec);
            if (ec) continue;
            LoadArchive(e.path().filename().string(), size,
                        [fp](uint64_t at, void* dst, size_t n) {
                            _fseeki64(fp.get(), int64_t(at), SEEK_SET);
                            return std::fread(dst, 1, n, fp.get());
                        },
                        out);
        }
    } else {  // the game from a disc image
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec))
            if (e.is_regular_file(ec)) {
                std::wstring ext = e.path().extension().wstring();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
                if (ext == L".iso") isos.push_back(e.path());
            }
        std::sort(isos.begin(), isos.end());
        for (const fs::path& iso : isos) {
            if (abort && *abort) return;
            auto d = std::make_unique<kernel::Disc>();
            std::string error;
            if (!d->Open(iso, error)) continue;
            const kernel::Disc::Entry* root = d->Find(std::vector<std::string>{});
            if (!root) continue;
            for (const kernel::Disc::Entry& e : root->children) {
                if (abort && *abort) return;
                if (e.dir || e.name.size() < 4 || _stricmp(e.name.c_str() + e.name.size() - 4, ".rcf") != 0) continue;
                kernel::Disc* disc = d.get();
                LoadArchive(e.name, e.size,
                            [disc, entry = e](uint64_t at, void* dst, size_t n) -> size_t {
                                size_t total = 0;
                                while (total < n) {
                                    uint32_t chunk = uint32_t(std::min<size_t>(n - total, 0x10000000));
                                    uint32_t got = disc->Read(entry, at + total, static_cast<uint8_t*>(dst) + total, chunk);
                                    total += got;
                                    if (got < chunk) break;
                                }
                                return total;
                            },
                            out);
            }
            out.disc = std::move(d);
            break;
        }
        if (!out.disc && out.archives.empty())
            out.error = "no game in " + assets.string() + ": put your disc image (.iso) there first";
    }
    std::sort(out.archives.begin(), out.archives.end(),
              [](const Archive& x, const Archive& y) { return NameLess(x.name, y.name); });
}

// ---------------------------------------------------------------- window state

constexpr int WM_APP_JOB = WM_APP + 1;      // the worker's result
constexpr int WM_APP_ACTIONS = WM_APP + 2;  // refresh the buttons' enable state

enum {
    ID_EXTRACT = 2001,
    ID_REPLACE,
    ID_REFRESH,
    ID_MOD_LABEL,
    ID_MOD_COMBO,
    ID_MOD_NEW,
    ID_SEARCH_LABEL,
    ID_SEARCH,
    ID_TAB,
    ID_TREE,
    ID_LIST,
    ID_MODS,
    ID_MODS_NEW,
    ID_MODS_OPEN,
    ID_MODS_LOG,
    ID_STATUS,
    ID_SEARCH_FOCUS = 3000,
};

enum Mode { MODE_BROWSE, MODE_SEARCH };
enum JobKind { JOB_LOAD, JOB_EXTRACT_FILE, JOB_EXTRACT_FOLDER, JOB_EXTRACT_ARCHIVE, JOB_REPLACE };

struct Job {
    JobKind kind = JOB_LOAD;
    uint32_t a = 0, f = 0;
    bool whole = false;      // replace the whole archive file, not a file inside it
    std::string prefix;      // folder extraction: the folder inside the archive
    fs::path dest;           // extract destination
    fs::path src;            // replace: the file on disk
    std::wstring mod;        // replace: the mod's folder name
};

struct JobResult {
    JobKind kind = JOB_LOAD;
    bool ok = false;
    std::wstring text;
    LoadResult* load = nullptr;  // a load's outcome, freed here
};

struct NodeData {
    bool folder = false;   // a folder or an archive root, not a file
    bool archive = false;  // an archive root
    uint32_t a = 0, f = 0;
    std::string path;  // a folder's prefix, or a file's name inside its archive
    bool expanded = false;
};

struct Row {
    uint32_t a = 0, f = 0;
    bool folder = false;
    std::wstring name;
};

HINSTANCE g_inst;
HWND g_hwnd, g_tree, g_list, g_mods, g_tab, g_status, g_search, g_combo;
HWND g_extract_btn, g_replace_btn, g_refresh_btn, g_mod_new_btn, g_mod_label_h, g_search_label_h;
HWND g_mods_new_btn, g_mods_open_btn, g_mods_log_btn;
HFONT g_font;
HACCEL g_accel;
std::vector<Archive> g_archives;
std::unique_ptr<kernel::Disc> g_disc;  // alive as long as the archives read from it
size_t g_file_count = 0;
fs::path g_assets, g_root, g_last_dir;
std::wstring g_summary;  // the resting status line: what the game and the mods are
bool g_load_ok = false, g_selftest = false;
Mode g_mode = MODE_BROWSE;
std::vector<Row> g_rows;
std::vector<ModRow> g_mod_rows;
bool g_mod_filling = false;  // filling the mod list: its own state changes count for nothing
std::atomic<bool> g_busy{false};
UINT g_dpi = 96;
std::thread g_worker;
fs::path g_arg_dir;  // a game folder from the command line, else empty

int Scale(int px) { return px * int(g_dpi) / 96; }

// the pieces the file tree and the jobs need from each other's sections
void UpdateActions();
void RefreshMods();
void PopulateList(NodeData* nd, uint32_t select_file = UINT32_MAX);
void ClearSearch();
void OnTreeSel(HTREEITEM h);
void SelfTestDone(bool failed, const std::wstring& note);
void StartJob(Job job);
void OnSearchEdit();

void SetStatus(const std::wstring& text) {
    if (g_status && IsWindow(g_status)) SetWindowTextW(g_status, text.c_str());
}

// ---------------------------------------------------------------- the file tree

struct Kid {
    std::string name;
    bool dir = false;
    uint32_t f = 0;
};

// the folders and files one level under a folder of an archive, folders first, both by name
void CollectKids(const Archive& a, const std::string& prefix, std::vector<Kid>& out) {
    std::vector<Kid> files;
    std::vector<std::string> dirs;
    for (uint32_t i = 0; i < a.rcf.files.size(); i++) {
        const std::string& n = a.rcf.files[i].name;
        if (!UnderFolder(n, prefix)) continue;
        std::string rest = prefix.empty() ? n : n.substr(prefix.size() + 1);
        size_t slash = rest.find('\\');
        if (slash == std::string::npos) files.push_back({rest, false, i});
        else dirs.push_back(rest.substr(0, slash));
    }
    std::sort(files.begin(), files.end(), [](const Kid& x, const Kid& y) { return NameLess(x.name, y.name); });
    std::sort(dirs.begin(), dirs.end(), NameLess);
    dirs.erase(std::unique(dirs.begin(), dirs.end(), [](const std::string& x, const std::string& y) {
                   return Folded(x) == Folded(y);
               }),
               dirs.end());
    out.reserve(dirs.size() + files.size());
    for (const std::string& d : dirs) out.push_back({d, true, 0});
    out.insert(out.end(), files.begin(), files.end());
}

NodeData* NodeOf(HTREEITEM h) {
    if (!h) return nullptr;
    TVITEMW it{};
    it.hItem = h;
    it.mask = TVIF_PARAM;
    if (!TreeView_GetItem(g_tree, &it)) return nullptr;
    return (NodeData*)it.lParam;
}

void InsertKids(NodeData* nd, HTREEITEM item) {
    const Archive& a = g_archives[nd->a];
    if (!a.is_rcf) return;
    std::vector<Kid> kids;
    CollectKids(a, nd->path, kids);
    bool dirs = false;
    for (const Kid& k : kids) {
        NodeData* child = new NodeData;
        child->folder = k.dir;
        child->archive = false;
        child->a = nd->a;
        child->f = k.f;
        child->path = k.dir ? (nd->path.empty() ? k.name : nd->path + '\\' + k.name) : a.rcf.files[k.f].name;
        wchar_t text[512];
        swprintf(text, 512, L"%.*hs", int(k.name.size()), k.name.c_str());
        TVINSERTSTRUCTW ins{};
        ins.hParent = item;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        ins.item.pszText = text;
        ins.item.lParam = (LPARAM)child;
        ins.item.cChildren = k.dir ? 1 : 0;
        TreeView_InsertItem(g_tree, &ins);
        dirs = dirs || k.dir;
    }
    TVITEMW it{};
    it.hItem = item;
    it.mask = TVIF_CHILDREN;
    it.cChildren = dirs ? 1 : 0;  // nothing under this folder: drop its +
    TreeView_SetItem(g_tree, &it);
}

void BuildTree() {
    for (size_t i = 0; i < g_archives.size(); i++) {
        const Archive& a = g_archives[i];
        auto* nd = new NodeData;
        nd->folder = true;
        nd->archive = true;
        nd->a = uint32_t(i);
        wchar_t text[512];
        if (a.is_rcf)
            swprintf(text, 512, L"%hs (%ls, %zu files)", a.name.c_str(), SizeText(a.size).c_str(), a.rcf.files.size());
        else
            swprintf(text, 512, L"%hs (not an archive)", a.name.c_str());
        TVINSERTSTRUCTW ins{};
        ins.hParent = TVI_ROOT;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_CHILDREN;
        ins.item.pszText = text;
        ins.item.lParam = (LPARAM)nd;
        ins.item.cChildren = a.is_rcf ? 1 : 0;
        TreeView_InsertItem(g_tree, &ins);
    }
}

// ---------------------------------------------------------------- the list: one folder's contents, or search results

struct ListCol {
    const wchar_t* title;
    int width;  // at 96 dpi, 0 = fill the rest
};
constexpr ListCol kListCols[] = {{L"Name", 0}, {L"Size", 96}, {L"Archive", 150}};
constexpr ListCol kModCols[] = {{L"Mod", 0}, {L"Files", 70}, {L"Size", 96}, {L"State", 70}};

void SetColumns(HWND list, const ListCol* cols, size_t count) {
    while (ListView_DeleteColumn(list, 0)) {}
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
    c.fmt = LVCFMT_LEFT;
    for (size_t i = 0; i < count; i++) {
        c.pszText = (wchar_t*)cols[i].title;
        c.cx = cols[i].width ? Scale(cols[i].width) : 100;
        ListView_InsertColumn(list, int(i), &c);
    }
}

void AddRow(HWND list, const std::wstring& name, const std::wstring& size, const std::wstring& arch) {
    LVITEMW it{};
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = ListView_GetItemCount(list);
    it.lParam = it.iItem;
    it.pszText = (wchar_t*)name.c_str();
    int at = ListView_InsertItem(list, &it);
    ListView_SetItemText(list, at, 1, (wchar_t*)size.c_str());
    ListView_SetItemText(list, at, 2, (wchar_t*)arch.c_str());
}

void AddFileRow(uint32_t ai, uint32_t fi, bool show_archive) {
    const Archive& a = g_archives[ai];
    g_rows.push_back({ai, fi, false, Widen(a.rcf.files[fi].name)});
    AddRow(g_list, g_rows.back().name, SizeText(a.rcf.Size(a.rcf.files[fi].entry)), show_archive ? Widen(a.name) : L"");
}

// the contents of a tree node's folder; a file node selects itself in its folder's list
void PopulateList(NodeData* nd, uint32_t select_file) {
    if (!g_list || !IsWindow(g_list)) return;
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    g_rows.clear();
    ListView_DeleteAllItems(g_list);
    if (nd && nd->folder) {
        const Archive& a = g_archives[nd->a];
        if (a.is_rcf) {
            std::vector<Kid> kids;
            CollectKids(a, nd->path, kids);
            for (const Kid& k : kids) {
                if (k.dir) {
                    g_rows.push_back({nd->a, 0, true, Widen(k.name)});
                    AddRow(g_list, Widen(k.name), L"—", L"");
                } else {
                    AddFileRow(nd->a, k.f, false);
                }
            }
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, FALSE);
    if (select_file != UINT32_MAX) {
        for (int i = 0; i < int(g_rows.size()); i++)
            if (!g_rows[i].folder && g_rows[i].f == select_file) {
                ListView_SetItemState(g_list, i, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                ListView_EnsureVisible(g_list, i, FALSE);
                return;
            }
    }
    if (nd && nd->folder && !nd->archive && !nd->path.empty())
        SetStatus(Widen(nd->path) + L" in " + Widen(g_archives[nd->a].name));
    else
        SetStatus(g_summary);
}

// search results across every archive
void RunSearch(const std::wstring& query) {
    g_mode = MODE_SEARCH;
    SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
    g_rows.clear();
    ListView_DeleteAllItems(g_list);
    std::string needle = Folded(NarrowUtf8(query));
    int shown = 0;
    bool capped = false;
    for (size_t ai = 0; ai < g_archives.size() && !capped; ai++) {
        const Archive& a = g_archives[ai];
        if (!a.is_rcf) continue;
        for (uint32_t i = 0; i < a.rcf.files.size(); i++) {
            if (a.folded[i].find(needle) == std::string::npos) continue;
            AddFileRow(uint32_t(ai), i, true);
            if (++shown >= kSearchLimit) {
                capped = true;
                break;
            }
        }
    }
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_list, nullptr, FALSE);
    SetStatus(std::to_wstring(shown) + (capped ? L"+ matches for \"" + query + L"\" — showing the first " +
                                                     std::to_wstring(kSearchLimit)
                                              : L" matches for \"" + query + L"\""));
}

void ClearSearch() {
    g_mode = MODE_BROWSE;
    SetWindowTextW(g_search, L"");
    KillTimer(g_hwnd, 2);
}

void OnSearchEdit() {
    wchar_t text[256];
    GetWindowTextW(g_search, text, 256);
    KillTimer(g_hwnd, 2);
    if (Trimmed(text).empty()) {
        if (g_mode == MODE_SEARCH) {
            g_mode = MODE_BROWSE;
            PopulateList(NodeOf(TreeView_GetSelection(g_tree)));
        }
        return;
    }
    SetTimer(g_hwnd, 2, 250, nullptr);  // a pause in typing, then the list follows the box
}

// ---------------------------------------------------------------- selection and actions

bool TabBrowser() { return !g_tab || TabCtrl_GetCurSel(g_tab) == 0; }

bool GetSelRow(Row& row) {
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (i < 0 || i >= int(g_rows.size())) return false;
    row = g_rows[i];
    return true;
}

NodeData* TreeSel() { return NodeOf(TreeView_GetSelection(g_tree)); }

void UpdateStatusSel() {
    Row row;
    if (GetSelRow(row) && !row.folder) {
        const Archive& a = g_archives[row.a];
        if (a.is_rcf) {
            const hulk::Rcf::File& f = a.rcf.files[row.f];
            SetStatus(Widen(f.name) + L" — " + SizeText(a.rcf.Size(f.entry)) + L" — inside " + Widen(a.name));
        }
        return;
    }
    if (g_mode == MODE_SEARCH) return;  // keep the search's summary
    NodeData* nd = TreeSel();
    if (nd && nd->archive) {
        const Archive& a = g_archives[nd->a];
        SetStatus(a.is_rcf ? Widen(a.name) + L" — " + SizeText(a.size) + L" — " + std::to_wstring(a.rcf.files.size()) +
                                 L" files"
                           : Widen(a.name) + L" — not an archive");
    } else {
        SetStatus(g_summary);
    }
}

void UpdateActions() {
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    bool browser = TabBrowser();
    Row row;
    bool file = browser && GetSelRow(row) && !row.folder;
    NodeData* nd = TreeSel();
    bool folder = browser && nd && nd->folder;
    EnableWindow(g_extract_btn, (file || folder) && !g_busy && g_load_ok);
    EnableWindow(g_replace_btn, file && !g_busy && g_load_ok);
    EnableWindow(g_refresh_btn, !g_busy);
    EnableWindow(g_search, browser);
}

std::wstring ComboMod() {
    wchar_t text[128];
    GetWindowTextW(g_combo, text, 128);
    return Trimmed(text);
}

void RememberDialogDir(const OPENFILENAMEW& o) {
    if (o.lpstrFile) {
        fs::path p(o.lpstrFile);
        if (p.has_parent_path()) g_last_dir = p.parent_path();
    }
}

bool GetSavePath(HWND parent, const wchar_t* name, fs::path& out) {
    wchar_t file[1024];
    wcsncpy(file, name, 1023);
    file[1023] = 0;
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = parent;
    o.lpstrFilter = L"All files (*.*)\0*.*\0";
    o.lpstrFile = file;
    o.nMaxFile = 1024;
    o.lpstrInitialDir = g_last_dir.empty() ? nullptr : g_last_dir.c_str();
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetSaveFileNameW(&o)) return false;
    RememberDialogDir(o);
    out = file;
    return true;
}

bool GetOpenPath(HWND parent, fs::path& out) {
    wchar_t file[1024] = L"";
    OPENFILENAMEW o{};
    o.lStructSize = sizeof(o);
    o.hwndOwner = parent;
    o.lpstrFilter = L"All files (*.*)\0*.*\0";
    o.lpstrFile = file;
    o.nMaxFile = 1024;
    o.lpstrInitialDir = g_last_dir.empty() ? nullptr : g_last_dir.c_str();
    o.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&o)) return false;
    RememberDialogDir(o);
    out = file;
    return true;
}

bool BrowseFolder(HWND parent, const wchar_t* title, fs::path& out) {
    wchar_t display[MAX_PATH] = L"";
    BROWSEINFOW bi{};
    bi.hwndOwner = parent;
    bi.pszDisplayName = display;
    bi.lpszTitle = title;
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
    PIDLIST_ABSOLUTE idl = SHBrowseForFolderW(&bi);
    if (!idl) return false;
    wchar_t path[MAX_PATH];
    bool ok = SHGetPathFromIDListW(idl, path) != 0;
    CoTaskMemFree(idl);
    if (ok) {
        out = path;
        g_last_dir = out;
    }
    return ok;
}

void DoExtract() {
    Row row;
    if (GetSelRow(row) && !row.folder) {
        const Archive& a = g_archives[row.a];
        fs::path dest;
        if (!GetSavePath(g_hwnd, LastComponent(a.rcf.files[row.f].name).c_str(), dest)) return;
        Job j;
        j.kind = JOB_EXTRACT_FILE;
        j.a = row.a;
        j.f = row.f;
        j.dest = dest;
        StartJob(std::move(j));
        return;
    }
    NodeData* nd = TreeSel();
    if (nd && nd->folder) {
        fs::path dir;
        if (!BrowseFolder(g_hwnd, L"Choose a folder for the extracted files", dir)) return;
        Job j;
        j.kind = nd->archive ? JOB_EXTRACT_ARCHIVE : JOB_EXTRACT_FOLDER;
        j.a = nd->a;
        j.prefix = nd->path;
        j.dest = dir;
        StartJob(std::move(j));
        return;
    }
    SetStatus(L"Select a file in the list, or a folder in the tree, first.");
}

std::wstring ModFromCombo() {
    std::wstring mod = ComboMod();
    if (mod.empty()) {
        SetStatus(L"Type a name for your mod in the Mod box first, e.g. My Mod.");
        SetFocus(g_combo);
        return {};
    }
    if (!ValidModName(mod)) {
        SetStatus(L"That name cannot be a mod folder: use letters, numbers, spaces and - _ ( ) only.");
        SetFocus(g_combo);
        return {};
    }
    return mod;
}

void DoReplace(bool whole) {
    Job j;
    j.kind = JOB_REPLACE;
    if (whole) {
        NodeData* nd = TreeSel();
        if (!nd || !nd->archive) return;
        j.a = nd->a;
        j.whole = true;
    } else {
        Row row;
        if (!GetSelRow(row) || row.folder) {
            SetStatus(L"Select a file first, or an archive in the tree to replace the whole file.");
            return;
        }
        j.a = row.a;
        j.f = row.f;
    }
    j.mod = ModFromCombo();
    if (j.mod.empty()) return;
    if (!GetOpenPath(g_hwnd, j.src)) return;
    StartJob(std::move(j));
}

// ---------------------------------------------------------------- the mod manager

void RefreshMods() {
    g_mod_rows = ScanMods(g_root);
    // the list
    g_mod_filling = true;
    SendMessageW(g_mods, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_mods);
    for (const ModRow& m : g_mod_rows) {
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = ListView_GetItemCount(g_mods);
        it.lParam = it.iItem;
        wchar_t name[280];
        swprintf(name, 280, L"%ls", m.name.c_str());
        it.pszText = name;
        int at = ListView_InsertItem(g_mods, &it);
        wchar_t files[32], state[16];
        swprintf(files, 32, L"%zu", m.files);
        swprintf(state, 16, L"%ls", m.enabled ? L"on" : L"off");
        ListView_SetItemText(g_mods, at, 1, files);
        std::wstring sz = SizeText(m.bytes);
        ListView_SetItemText(g_mods, at, 2, sz.data());
        ListView_SetItemText(g_mods, at, 3, state);
        ListView_SetCheckState(g_mods, at, m.enabled);
    }
    SendMessageW(g_mods, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_mods, nullptr, FALSE);
    g_mod_filling = false;
    // the Mod box in the toolbar: keep what it held when the name is still there
    std::wstring had = ComboMod();
    ComboBox_ResetContent(g_combo);
    int pick = -1;
    for (size_t i = 0; i < g_mod_rows.size(); i++) {
        ComboBox_AddString(g_combo, g_mod_rows[i].name.c_str());
        if (!had.empty() && _wcsicmp(g_mod_rows[i].name.c_str(), had.c_str()) == 0) pick = int(i);
        if (pick < 0 && had.empty() && g_mod_rows[i].enabled) pick = int(i);
    }
    if (pick >= 0) ComboBox_SetCurSel(g_combo, pick);
}

void ToggleMod(size_t i) {
    if (i >= g_mod_rows.size()) return;
    const ModRow& m = g_mod_rows[i];
    std::wstring target = m.enabled ? L"_" + m.name : m.name.substr(m.name[0] == L'_' ? 1 : 0);
    std::error_code ec;
    fs::rename(g_root / L"mods" / m.name, g_root / L"mods" / target, ec);
    if (ec) {
        MessageBoxW(g_hwnd,
                    (L"Cannot turn " + (m.enabled ? std::wstring(L"off ") : std::wstring(L"on ")) + m.name + L":\n" +
                     Widen(ec.message()) +
                     L"\n\nThe game may still be running, or the folder is open somewhere.")
                        .c_str(),
                    L"Hulk Mod Studio", MB_ICONWARNING);
    } else {
        SetStatus(m.enabled ? L"Mod " + m.name + L" turned off (renamed to " + target + L")."
                            : L"Mod " + target + L" turned on.");
    }
    RefreshMods();
}

// a tiny modal: a name to create a mod folder from
struct DlgBuf {
    std::vector<WORD> w;
    void D(WORD v) { w.push_back(v); }
    void D32(DWORD v) {
        while (w.size() & 1) w.push_back(0);  // dwords sit on even word counts
        w.push_back(LOWORD(v));
        w.push_back(HIWORD(v));
    }
    void Str(const wchar_t* s) {
        while (*s) w.push_back(WORD(*s++));
        w.push_back(0);
    }
    void Ord(WORD v) {
        w.push_back(0xFFFF);
        w.push_back(v);
    }
};

std::wstring g_new_mod;

INT_PTR CALLBACK NewModProc(HWND dlg, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        HWND edit = GetDlgItem(dlg, 201);
        SendMessageW(edit, EM_LIMITTEXT, 64, 0);
        SetFocus(edit);
        return FALSE;  // the focus is set here
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK || LOWORD(wp) == IDCANCEL) {
            if (LOWORD(wp) == IDOK) {
                wchar_t text[128];
                GetDlgItemTextW(dlg, 201, text, 128);
                std::wstring name = Trimmed(text);
                if (!ValidModName(name)) {
                    MessageBoxW(dlg, L"Use letters, numbers, spaces and - _ ( ) for the name.", L"Hulk Mod Studio",
                                MB_ICONINFORMATION);
                    return TRUE;
                }
                g_new_mod = name;
            }
            EndDialog(dlg, LOWORD(wp) == IDOK ? 1 : 0);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dlg, 0);
        return TRUE;
    }
    return FALSE;
}

bool AskNewMod(HWND parent, std::wstring& name) {
    DlgBuf b;
    b.D(0);  // version
    b.D(0);  // signature
    b.D32(0);
    b.D32(0);
    b.D32(WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | DS_SETFONT | DS_CENTER);
    b.D32(4);  // items
    b.D32(20);
    b.D32(20);
    b.D32(180);
    b.D32(66);
    b.D(0);  // menu
    b.D(0);  // class
    b.Str(L"New Mod");
    b.D32(9);
    b.Str(L"Segoe UI");
    // the label
    b.D32(0);
    b.D32(0);
    b.D32(WS_CHILD | WS_VISIBLE | SS_LEFT);
    b.D32(10);
    b.D32(7);
    b.D32(160);
    b.D32(9);
    b.D32(200);
    b.Ord(0x0082);
    b.Str(L"Name of your mod:");
    b.D(0);
    // the edit
    b.D32(0);
    b.D32(WS_EX_CLIENTEDGE);
    b.D32(WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL);
    b.D32(10);
    b.D32(19);
    b.D32(160);
    b.D32(12);
    b.D32(201);
    b.Ord(0x0081);
    b.Str(L"");
    b.D(0);
    // OK
    b.D32(0);
    b.D32(0);
    b.D32(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON);
    b.D32(82);
    b.D32(40);
    b.D32(44);
    b.D32(14);
    b.D32(IDOK);
    b.Ord(0x0080);
    b.Str(L"Create");
    b.D(0);
    // Cancel
    b.D32(0);
    b.D32(0);
    b.D32(WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON);
    b.D32(132);
    b.D32(40);
    b.D32(44);
    b.D32(14);
    b.D32(IDCANCEL);
    b.Ord(0x0080);
    b.Str(L"Cancel");
    b.D(0);
    if (DialogBoxIndirectParamW(GetModuleHandleW(nullptr), (DLGTEMPLATE*)b.w.data(), parent, NewModProc, 0) == 1) {
        name = g_new_mod;
        return true;
    }
    return false;
}

void DoNewMod() {
    if (g_root.empty()) {
        MessageBoxW(g_hwnd, L"Open the game's folder first: put Hulk Mod Studio.exe beside its assets folder.",
                    L"Hulk Mod Studio", MB_ICONINFORMATION);
        return;
    }
    std::wstring name;
    if (!AskNewMod(g_hwnd, name)) return;
    std::error_code ec;
    fs::create_directories(g_root / L"mods" / name, ec);
    if (ec) {
        MessageBoxW(g_hwnd, (L"Cannot create the folder:\n" + Widen(ec.message())).c_str(), L"Hulk Mod Studio",
                    MB_ICONWARNING);
        return;
    }
    RefreshMods();
    int at = ComboBox_FindStringExact(g_combo, -1, name.c_str());
    if (at >= 0) ComboBox_SetCurSel(g_combo, at);
    SetStatus(L"Mod " + name + L" created. Extract what it replaces, then use Replace in Mod.");
}

void OpenMods() {
    if (g_root.empty()) {
        MessageBoxW(g_hwnd, L"Open the game's folder first: put Hulk Mod Studio.exe beside its assets folder.",
                    L"Hulk Mod Studio", MB_ICONINFORMATION);
        return;
    }
    std::error_code ec;
    fs::path mods = g_root / L"mods";
    if (!fs::is_directory(mods, ec)) fs::create_directories(mods, ec);
    ShellExecuteW(g_hwnd, L"open", mods.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void OpenLog() {
    if (g_root.empty()) {
        MessageBoxW(g_hwnd, L"Open the game's folder first: put Hulk Mod Studio.exe beside its assets folder.",
                    L"Hulk Mod Studio", MB_ICONINFORMATION);
        return;
    }
    fs::path log = FindLog(g_root);
    if (!log.empty()) {
        ShellExecuteW(g_hwnd, L"open", log.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    MessageBoxW(g_hwnd,
                L"The log appears once the game has run:\n"
                L"  game\\logs\\hulk.log in the game's folder\n"
                L"or logs\\hulk.log in a build's output.\n"
                L"Start the game, then open it here. It lists each mod file the game takes.",
                L"Hulk Mod Studio", MB_ICONINFORMATION);
}

// ---------------------------------------------------------------- jobs: the worker thread

// Jobs run one at a time on a worker thread. The archives a job reads only change in the job-done message on the UI
// thread, and a new job cannot start until that ran, so a job always sees the archives it started with.
void RunJob(Job job) {
    auto* r = new JobResult{job.kind};
    std::wstring& text = r->text;
    switch (job.kind) {
    case JOB_LOAD: {
        r->load = new LoadResult;
        LoadGameInto(ResolveAssets(g_arg_dir), *r->load, &g_abort);
        if (r->load->error.empty()) {
            r->ok = true;
            text = L"Loaded " + std::to_wstring(r->load->archives.size()) + L" archives, " +
                   std::to_wstring(r->load->files) + L" files";
        } else {
            text = Widen(r->load->error);
        }
        break;
    }
    case JOB_EXTRACT_FILE: {
        const Archive& a = g_archives[job.a];
        const hulk::Rcf::File& f = a.rcf.files[job.f];
        r->ok = ExtractFile(a, job.f, job.dest, text, &g_abort);
        if (r->ok)
            text = L"Extracted " + Widen(f.name) + L" (" + SizeText(a.rcf.Size(f.entry)) + L") to " + job.dest.wstring();
        break;
    }
    case JOB_EXTRACT_FOLDER: {
        size_t written = 0, kept = 0, failed = 0;
        ExtractFolder(g_archives[job.a], job.prefix, job.dest, written, kept, failed, text, &g_abort);
        r->ok = failed == 0 && !g_abort;
        if (text.empty())
            text = L"Extracted " + std::to_wstring(written) + L" files to " + job.dest.wstring() + L" (already there: " +
                   std::to_wstring(kept) + L", failed: " + std::to_wstring(failed) + L")";
        break;
    }
    case JOB_EXTRACT_ARCHIVE: {
        r->ok = ExtractWholeArchive(g_archives[job.a], job.dest, text, &g_abort);
        if (r->ok)
            text = L"Extracted " + Widen(g_archives[job.a].name) + L" (" + SizeText(g_archives[job.a].size) +
                   L") to " + job.dest.wstring();
        break;
    }
    case JOB_REPLACE: {
        const Archive& a = g_archives[job.a];
        const std::string& internal = job.whole ? a.name : a.rcf.files[job.f].name;
        r->ok = ReplaceInMod(g_root, internal, job.src, job.mod, text);
        if (r->ok) text = L"Replaced " + Widen(internal) + L" in mods\\" + job.mod + L" — start the game to see it";
        break;
    }
    }
    g_busy = false;
    if (g_hwnd && IsWindow(g_hwnd)) PostMessageW(g_hwnd, WM_APP_JOB, 0, (LPARAM)r);
    else delete r;
}

void StartJob(Job job) {
    if (g_busy) return;
    if (g_worker.joinable()) g_worker.join();
    g_busy = true;
    SetStatus(L"Working…");
    g_worker = std::thread(RunJob, std::move(job));
    UpdateActions();
}

void TakeLoad(LoadResult& l) {
    ClearSearch();
    TreeView_DeleteAllItems(g_tree);
    g_rows.clear();
    ListView_DeleteAllItems(g_list);
    g_archives = std::move(l.archives);
    g_disc = std::move(l.disc);
    g_assets = l.assets;
    g_root = l.root;
    g_file_count = l.files;
    g_load_ok = !g_archives.empty();
    BuildTree();
    RefreshMods();
    g_summary = g_load_ok ? std::to_wstring(g_archives.size()) + L" archives, " + std::to_wstring(g_file_count) +
                                L" files — " + g_root.wstring()
                          : L"No game found. Put the game's files or a disc image in an assets folder.";
    SetStatus(g_summary);
    std::wstring title = L"Hulk Mod Studio";
    if (!g_root.empty()) title += L" — " + g_root.wstring();
    SetWindowTextW(g_hwnd, title.c_str());
    UpdateActions();
}

void OnJobDone(JobResult* r) {
    std::unique_ptr<JobResult> done(r);
    if (!g_hwnd || !IsWindow(g_hwnd)) return;
    if (r->kind == JOB_LOAD && r->load) {
        TakeLoad(*r->load);
        delete r->load;
        r->load = nullptr;
    }
    if (r->kind == JOB_REPLACE && r->ok) RefreshMods();
    if (!r->text.empty()) SetStatus(r->text);
    g_busy = false;
    UpdateActions();
    if (g_selftest && r->kind == JOB_LOAD) SelfTestDone(!r->ok, r->text);
}

void SelfTestDone(bool failed, const std::wstring& note) {
    KillTimer(g_hwnd, 1);
    if (failed) {
        std::fwprintf(stderr, L"selftest failed: %ls\n", note.c_str());
        std::fflush(stderr);
        PostQuitMessage(1);
        return;
    }
    std::wprintf(L"selftest ok: %zu archives, %zu files, %zu tree roots, %zu mods\n", g_archives.size(), g_file_count,
                 size_t(TreeView_GetCount(g_tree)), g_mod_rows.size());
    std::fflush(stdout);
    PostQuitMessage(0);
}

// ---------------------------------------------------------------- layout and creation

HFONT MakeUiFont() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    return CreateFontIndirectW(&ncm.lfMessageFont);
}

void ApplyFont(HWND h) { SendMessageW(h, WM_SETFONT, (WPARAM)g_font, TRUE); }

HWND MakeButton(int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_PUSHBUTTON, 0, 0, 0, 0, g_hwnd,
                             (HMENU)(intptr_t)id, g_inst, nullptr);
    ApplyFont(h);
    return h;
}

HWND MakeLabel(int id, const wchar_t* text) {
    HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, g_hwnd,
                             (HMENU)(intptr_t)id, g_inst, nullptr);
    ApplyFont(h);
    return h;
}

void ShowPage() {
    bool browser = TabBrowser();
    ShowWindow(g_tree, browser ? SW_SHOWNOACTIVATE : SW_HIDE);
    ShowWindow(g_list, browser ? SW_SHOWNOACTIVATE : SW_HIDE);
    ShowWindow(g_mods, browser ? SW_HIDE : SW_SHOWNOACTIVATE);
    ShowWindow(g_mods_new_btn, browser ? SW_HIDE : SW_SHOWNOACTIVATE);
    ShowWindow(g_mods_open_btn, browser ? SW_HIDE : SW_SHOWNOACTIVATE);
    ShowWindow(g_mods_log_btn, browser ? SW_HIDE : SW_SHOWNOACTIVATE);
}

void DoLayout() {
    RECT rc;
    GetClientRect(g_hwnd, &rc);
    int cx = rc.right, cy = rc.bottom;
    int pad = Scale(8), barh = Scale(28), sth = Scale(22);
    HDWP d = BeginDeferWindowPos(16);
    auto place = [&](HWND h, int x, int y, int w, int hgt) {
        if (h) DeferWindowPos(d, h, nullptr, x, y, w, hgt, SWP_NOZORDER | SWP_NOACTIVATE);
    };
    // the toolbar row: actions, the mod the work lands in, the search
    int y = pad;
    int x = pad;
    place(g_extract_btn, x, y, Scale(96), barh);
    x += Scale(96) + Scale(6);
    place(g_replace_btn, x, y, Scale(132), barh);
    x += Scale(132) + Scale(14);
    place(g_refresh_btn, x, y, Scale(88), barh);
    x += Scale(88) + Scale(16);
    place(g_mod_label_h, x, y + Scale(7), Scale(30), Scale(16));
    x += Scale(30) + Scale(4);
    place(g_combo, x, y, Scale(170), barh + Scale(120));
    x += Scale(170) + Scale(4);
    place(g_mod_new_btn, x, y, Scale(24), barh);
    x += Scale(24) + Scale(16);
    place(g_search_label_h, x, y + Scale(7), Scale(48), Scale(16));
    x += Scale(48) + Scale(4);
    place(g_search, x, y, cx - x - pad, barh);
    // the tabs fill the rest, the status line sits at the bottom
    int taby = y + barh + Scale(8);
    int tabh = cy - taby - sth - pad;
    place(g_tab, 0, taby, cx, tabh);
    RECT inner = {0, 0, cx, tabh};
    SendMessageW(g_tab, TCM_ADJUSTRECT, FALSE, (LPARAM)&inner);
    int px = inner.left, py = taby + inner.top;
    int pw = inner.right - inner.left, ph = inner.bottom - inner.top;
    int treew = std::max(Scale(240), pw * 2 / 5);
    place(g_tree, px, py, treew, ph);
    place(g_list, px + treew + Scale(6), py, pw - treew - Scale(6), ph);
    int by = py + Scale(4);
    place(g_mods_new_btn, px, by, Scale(96), Scale(26));
    place(g_mods_open_btn, px + Scale(104), by, Scale(140), Scale(26));
    place(g_mods_log_btn, px + Scale(252), by, Scale(130), Scale(26));
    place(g_mods, px, by + Scale(26) + Scale(6), pw, std::max(ph - Scale(32) - Scale(6), Scale(80)));
    place(g_status, pad, cy - sth - Scale(2), cx - 2 * pad, sth);
    EndDeferWindowPos(d);
    // column widths: Name fills, the rest are fixed
    ListView_SetColumnWidth(g_list, 0, std::max(pw - treew - Scale(6) - Scale(96) - Scale(150) - Scale(30), Scale(120)));
    ListView_SetColumnWidth(g_list, 1, Scale(96));
    ListView_SetColumnWidth(g_list, 2, Scale(150));
    ListView_SetColumnWidth(g_mods, 0, std::max(pw - Scale(70) - Scale(96) - Scale(70) - Scale(40), Scale(100)));
    ListView_SetColumnWidth(g_mods, 1, Scale(70));
    ListView_SetColumnWidth(g_mods, 2, Scale(96));
    ListView_SetColumnWidth(g_mods, 3, Scale(70));
}

LRESULT CALLBACK SearchProc(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t text[256];
        GetWindowTextW(h, text, 256);
        if (Trimmed(text).empty()) ClearSearch();
        else {
            KillTimer(g_hwnd, 2);
            RunSearch(text);
        }
        return 0;
    }
    if (m == WM_KEYDOWN && wp == VK_ESCAPE) {
        ClearSearch();
        PopulateList(NodeOf(TreeView_GetSelection(g_tree)));
        return 0;
    }
    return DefSubclassProc(h, m, wp, lp);
}

void CreateChildren() {
    g_font = MakeUiFont();
    g_extract_btn = MakeButton(ID_EXTRACT, L"Extract...");
    g_replace_btn = MakeButton(ID_REPLACE, L"Replace in Mod...");
    g_refresh_btn = MakeButton(ID_REFRESH, L"Refresh");
    g_mod_label_h = MakeLabel(ID_MOD_LABEL, L"Mod:");
    g_combo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWN | CBS_AUTOHSCROLL,
                              0, 0, 0, 0, g_hwnd, (HMENU)(intptr_t)ID_MOD_COMBO, g_inst, nullptr);
    ApplyFont(g_combo);
    g_mod_new_btn = MakeButton(ID_MOD_NEW, L"+");
    g_search_label_h = MakeLabel(ID_SEARCH_LABEL, L"Search:");
    g_search = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, g_hwnd,
                               (HMENU)(intptr_t)ID_SEARCH, g_inst, nullptr);
    ApplyFont(g_search);
    SetWindowSubclass(g_search, SearchProc, 1, 0);
    SendMessageW(g_search, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search all archives (Enter)");
    g_tab = CreateWindowExW(WS_EX_COMPOSITED, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP,
                            0, 0, 0, 0, g_hwnd, (HMENU)(intptr_t)ID_TAB, g_inst, nullptr);
    ApplyFont(g_tab);
    TCITEMW tab{};
    tab.mask = TCIF_TEXT;
    tab.pszText = (wchar_t*)L"Archive Browser";
    TabCtrl_InsertItem(g_tab, 0, &tab);
    tab.pszText = (wchar_t*)L"Mod Manager";
    TabCtrl_InsertItem(g_tab, 1, &tab);
    g_tree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TVS_HASLINES | TVS_LINESATROOT |
                                 TVS_HASBUTTONS | TVS_SHOWSELALWAYS | TVS_DISABLEDRAGDROP,
                             0, 0, 0, 0, g_hwnd, (HMENU)(intptr_t)ID_TREE, g_inst, nullptr);
    ApplyFont(g_tree);
    g_list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                             0, 0, 0, 0, g_hwnd, (HMENU)(intptr_t)ID_LIST, g_inst, nullptr);
    ApplyFont(g_list);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    SetColumns(g_list, kListCols, 3);
    g_mods = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                                 LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                             0, 0, 0, 0, g_hwnd, (HMENU)(intptr_t)ID_MODS, g_inst, nullptr);
    ApplyFont(g_mods);
    ListView_SetExtendedListViewStyle(g_mods, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER);
    SetColumns(g_mods, kModCols, 4);
    g_mods_new_btn = MakeButton(ID_MODS_NEW, L"New Mod...");
    g_mods_open_btn = MakeButton(ID_MODS_OPEN, L"Open Mods Folder");
    g_mods_log_btn = MakeButton(ID_MODS_LOG, L"Open Game Log");
    g_status = CreateWindowExW(WS_EX_STATICEDGE, L"STATIC", L"Starting…",
                               WS_CHILD | WS_VISIBLE | SS_ENDELLIPSIS | SS_NOPREFIX, 0, 0, 0, 0, g_hwnd,
                               (HMENU)(intptr_t)ID_STATUS, g_inst, nullptr);
    ApplyFont(g_status);
    ShowPage();
}

// the tree's selection mirrors into the list, which is what the actions work on
void OnTreeSel(HTREEITEM h) {
    NodeData* nd = NodeOf(h);
    if (!nd) {
        PopulateList(nullptr);
        return;
    }
    if (!nd->folder) {
        NodeData* pnd = NodeOf(TreeView_GetParent(g_tree, h));
        PopulateList(pnd, nd->f);
        return;
    }
    PopulateList(nd);
}

void SelectTreeFolder(int row) {
    HTREEITEM parent = TreeView_GetSelection(g_tree);
    if (!parent) return;
    for (HTREEITEM c = TreeView_GetChild(g_tree, parent); c; c = TreeView_GetNextSibling(g_tree, c)) {
        wchar_t text[512];
        TVITEMW it{};
        it.hItem = c;
        it.mask = TVIF_TEXT;
        it.pszText = text;
        it.cchTextMax = 512;
        if (!TreeView_GetItem(g_tree, &it)) continue;
        if (_wcsicmp(text, g_rows[row].name.c_str()) == 0) {
            TreeView_Expand(g_tree, c, TVE_EXPAND);
            TreeView_SelectItem(g_tree, c);
            return;
        }
    }
}

void DoRefresh() {
    if (g_busy) return;
    SetStatus(L"Loading the game's archives…");
    StartJob(Job{});
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        switch (id) {
        case ID_EXTRACT: DoExtract(); return 0;
        case ID_REPLACE: DoReplace(false); return 0;
        case ID_REFRESH: DoRefresh(); return 0;
        case ID_MOD_NEW:
        case ID_MODS_NEW: DoNewMod(); return 0;
        case ID_MODS_OPEN: OpenMods(); return 0;
        case ID_MODS_LOG: OpenLog(); return 0;
        case ID_SEARCH_FOCUS: SetFocus(g_search); return 0;
        case ID_SEARCH: if (code == EN_CHANGE) OnSearchEdit(); return 0;
        }
        return 0;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        switch (nm->idFrom) {
        case ID_TAB:
            if (nm->code == TCN_SELCHANGE) {
                ShowPage();
                UpdateActions();
            }
            break;
        case ID_TREE:
            if (nm->code == TVN_ITEMEXPANDING) {
                NMTREEVIEW* t = (NMTREEVIEW*)nm;
                NodeData* nd = (NodeData*)t->itemNew.lParam;
                if (nd && nd->folder && !nd->expanded) {
                    nd->expanded = true;
                    InsertKids(nd, t->itemNew.hItem);
                }
            } else if (nm->code == TVN_SELCHANGED) {
                NMTREEVIEW* t = (NMTREEVIEW*)nm;
                ClearSearch();
                OnTreeSel(t->itemNew.hItem);
                UpdateActions();
            } else if (nm->code == TVN_DELETEITEM) {
                NMTREEVIEW* t = (NMTREEVIEW*)nm;
                delete (NodeData*)t->itemOld.lParam;
            }
            break;
        case ID_LIST:
            if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* l = (NMLISTVIEW*)nm;
                if (l->uChanged & LVIF_STATE) {
                    UpdateStatusSel();
                    UpdateActions();
                }
            } else if (nm->code == NM_DBLCLK) {
                NMLISTVIEW* l = (NMLISTVIEW*)nm;
                if (l->iItem >= 0 && l->iItem < int(g_rows.size())) {
                    if (g_rows[l->iItem].folder) SelectTreeFolder(l->iItem);
                    else DoExtract();
                }
            }
            break;
        case ID_MODS:
            if (nm->code == LVN_ITEMCHANGED) {
                NMLISTVIEW* l = (NMLISTVIEW*)nm;
                if (!g_mod_filling && (l->uChanged & LVIF_STATE) &&
                    ((l->uNewState ^ l->uOldState) & LVIS_STATEIMAGEMASK) && l->iItem >= 0 &&
                    l->iItem < int(g_mod_rows.size())) {
                    bool want = ListView_GetCheckState(g_mods, l->iItem) != 0;
                    if (want != g_mod_rows[l->iItem].enabled) ToggleMod(size_t(l->iItem));
                }
            }
            break;
        }
        return 0;
    }
    case WM_TIMER:
        if (wp == 2) {
            KillTimer(hwnd, 2);
            wchar_t text[256];
            GetWindowTextW(g_search, text, 256);
            if (!Trimmed(text).empty()) RunSearch(text);
        } else if (wp == 1) {
            std::wprintf(L"selftest: timed out\n");
            std::fflush(stdout);
            PostQuitMessage(2);
        }
        return 0;
    case WM_APP_ACTIONS: UpdateActions(); return 0;
    case WM_APP_JOB: OnJobDone((JobResult*)lp); return 0;
    case WM_SIZE: DoLayout(); return 0;
    case WM_SETFOCUS: SetFocus(g_tree); return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize = {Scale(880), Scale(560)};
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        if (g_font) DeleteObject(g_font);
        g_font = MakeUiFont();
        EnumChildWindows(hwnd, [](HWND h, LPARAM font) -> BOOL {
            SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
            return TRUE;
        }, (LPARAM)g_font);
        auto* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        g_abort = true;
        if (g_worker.joinable()) g_worker.join();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- the window-less flags

void PrintLine(const std::wstring& s) {
    std::wprintf(L"%ls\n", s.c_str());
    std::fflush(stdout);
}

const Archive* FindArchiveByName(const std::vector<Archive>& archives, const std::wstring& name) {
    std::string want = Folded(NarrowUtf8(name));
    for (const Archive& a : archives)
        if (Folded(a.name) == want) return &a;
    return nullptr;
}

int ConsoleList() {
    LoadResult l;
    LoadGameInto(ResolveAssets(g_arg_dir), l, nullptr);
    if (!l.error.empty()) {
        std::fwprintf(stderr, L"error: %hs\n", l.error.c_str());
        return 1;
    }
    for (const Archive& a : l.archives)
        std::wprintf(L"ARCHIVE\t%hs\t%zu\t%llu\t%ls\n", a.name.c_str(), a.is_rcf ? a.rcf.files.size() : size_t(0),
                     (unsigned long long)a.size, a.is_rcf ? L"ok" : L"not-an-archive");
    for (const ModRow& m : ScanMods(l.root))
        std::wprintf(L"MOD\t%ls\t%ls\t%zu\n", m.name.c_str(), m.enabled ? L"enabled" : L"disabled", m.files);
    std::wprintf(L"TOTAL\tarchives=%zu\tfiles=%zu\n", l.archives.size(), l.files);
    std::fflush(stdout);
    return 0;
}

// the shared tail of --extract and --replace: the game loaded, the archive and the file inside it found
bool ConsoleTarget(const std::vector<std::wstring>& args, size_t dir_arg, LoadResult& l, uint32_t& ai, uint32_t& fi) {
    LoadGameInto(ResolveAssets(dir_arg < args.size() ? fs::path(args[dir_arg]) : fs::path()), l, nullptr);
    if (!l.error.empty()) {
        std::fwprintf(stderr, L"error: %hs\n", l.error.c_str());
        return false;
    }
    const Archive* a = FindArchiveByName(l.archives, args[1]);
    if (!a || !a->is_rcf) {
        std::fwprintf(stderr, L"error: no archive %ls (use --list for the names)\n", args[1].c_str());
        return false;
    }
    ai = uint32_t(a - l.archives.data());
    std::string want = Folded(NarrowUtf8(args[2]));
    fi = UINT32_MAX;
    for (uint32_t i = 0; i < a->rcf.files.size(); i++)
        if (a->folded[i] == want) fi = i;
    if (fi == UINT32_MAX) {
        std::fwprintf(stderr, L"error: %hs holds %zu files and none is named %ls\n", a->name.c_str(),
                      a->rcf.files.size(), args[2].c_str());
        return false;
    }
    return true;
}

int ConsoleExtract(const std::vector<std::wstring>& args) {
    LoadResult l;
    uint32_t ai, fi;
    if (!ConsoleTarget(args, 4, l, ai, fi)) return 1;
    std::wstring err;
    if (!ExtractFile(l.archives[ai], fi, fs::path(args[3]), err, nullptr)) {
        std::fwprintf(stderr, L"error: %ls\n", err.c_str());
        return 1;
    }
    PrintLine(L"extracted " + Widen(l.archives[ai].rcf.files[fi].name) + L" (" +
              SizeText(l.archives[ai].rcf.Size(l.archives[ai].rcf.files[fi].entry)) + L") to " + args[3]);
    return 0;
}

int ConsoleReplace(const std::vector<std::wstring>& args) {
    LoadResult l;
    uint32_t ai, fi;
    if (!ConsoleTarget(args, 5, l, ai, fi)) return 1;
    std::wstring err;
    if (!ReplaceInMod(l.root, l.archives[ai].rcf.files[fi].name, fs::path(args[3]), args[4], err)) {
        std::fwprintf(stderr, L"error: %ls\n", err.c_str());
        return 1;
    }
    PrintLine(L"replaced " + Widen(l.archives[ai].rcf.files[fi].name) + L" in mods\\" + args[4]);
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** raw = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (raw) {
        for (int i = 1; i < argc; i++) args.emplace_back(raw[i]);
        LocalFree(raw);
    }

    if (!args.empty() && args[0] == L"--list") {
        if (args.size() > 1) g_arg_dir = args[1];
        return ConsoleList();
    }
    if (!args.empty() && args[0] == L"--extract" && args.size() >= 4) return ConsoleExtract(args);
    if (!args.empty() && args[0] == L"--replace" && args.size() >= 5) return ConsoleReplace(args);

    g_inst = inst;
    if (!args.empty() && args[0] == L"--selftest") {
        g_selftest = true;
        if (args.size() > 1) g_arg_dir = args[1];
    } else if (!args.empty()) {
        g_arg_dir = args[0];  // a game folder dragged onto the exe
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    // per-monitor dpi when the system offers it, so the controls stay crisp
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        using SetThreadDpiFn = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
        if (auto set = (SetThreadDpiFn)GetProcAddress(user32, "SetThreadDpiAwarenessContext"))
            set(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else if (auto sp = (BOOL(WINAPI*)())GetProcAddress(user32, "SetProcessDPIAware")) sp();
    }
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES |
                                          ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"HulkModStudio";
    RegisterClassW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"Hulk Mod Studio", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             1100, 720, nullptr, nullptr, inst, nullptr);
    if (HMODULE user32 = GetModuleHandleW(L"user32.dll")) {
        if (auto dpi = (UINT(WINAPI*)(HWND))GetProcAddress(user32, "GetDpiForWindow")) g_dpi = dpi(g_hwnd);
    }
    CreateChildren();
    DoLayout();
    ACCEL acc[] = {{FVIRTKEY, VK_F5, ID_REFRESH}, {FVIRTKEY | FCONTROL, 'F', ID_SEARCH_FOCUS}};
    g_accel = CreateAcceleratorTableW(acc, 2);

    if (!g_selftest) ShowWindow(g_hwnd, SW_SHOW);
    else SetTimer(g_hwnd, 1, 30000, nullptr);  // a hidden run must not outlive a stuck load
    StartJob(Job{});  // the load

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0)
        if (!TranslateAcceleratorW(g_hwnd, g_accel, &m)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    if (g_worker.joinable()) g_worker.join();
    CoUninitialize();
    return int(m.wParam);
}
