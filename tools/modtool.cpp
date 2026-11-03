// Hulk Mod Tool.exe: copies every file out of the game's archives into a folder, with the paths a mod uses
// (src/common/mods.h). It reads the game in assets like the game does: the extracted files, else the first disc image.
//   Hulk Mod Tool.exe                 extract to "Game Files" beside the assets folder
//   Hulk Mod Tool.exe <folder>        extract to that folder
// Files already there are left alone.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
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

namespace fs = std::filesystem;

namespace {

fs::path ExeDir() {
    std::wstring p(32768, L'\0');
    p.resize(GetModuleFileNameW(nullptr, p.data(), DWORD(p.size())));
    return fs::path(p).parent_path();
}

fs::path FindAssets() {
    fs::path dir = ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

// one archive of the game: its name, size and a read
struct Source {
    std::string name;
    uint64_t size;
    hulk::ReadFn read;
};

size_t g_written = 0, g_kept = 0, g_failed = 0;

void Extract(const Source& src, const fs::path& out, std::ofstream& index) {
    std::optional<hulk::Rcf> rcf = hulk::ReadRcf(src.size, src.read);
    if (!rcf) {
        std::cout << "  " << src.name << ": not an archive, skipped\n";
        return;
    }
    std::cout << "  " << src.name << ": " << rcf->files.size() << " files\n" << std::flush;
    std::vector<uint8_t> buf;
    for (const hulk::Rcf::File& f : rcf->files) {
        index << f.name << "\t" << src.name << "\t" << rcf->Size(f.entry) << "\r\n";
        std::string rel = f.name;
        std::replace(rel.begin(), rel.end(), '\\', '/');
        if (rel.find("..") != std::string::npos || rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos) {
            g_failed++;
            continue;
        }
        fs::path target = out / fs::path(rel).make_preferred();
        std::error_code ec;
        if (fs::exists(target, ec)) {
            g_kept++;
            continue;
        }
        fs::create_directories(target.parent_path(), ec);
        buf.resize(rcf->Size(f.entry));
        size_t got = src.read(rcf->Offset(f.entry), buf.data(), buf.size());
        std::ofstream file(target, std::ios::binary);
        if (got != buf.size() || !file.write(reinterpret_cast<const char*>(buf.data()), buf.size())) {
            g_failed++;
            continue;
        }
        g_written++;
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    fs::path assets = FindAssets();
    if (assets.empty()) {
        std::cout << "No assets folder found beside or above this tool.\n";
        std::cin.get();
        return 1;
    }
    fs::path out = argc > 1 ? fs::path(argv[1]) : assets.parent_path() / "Game Files";
    std::error_code ec;
    fs::create_directories(out, ec);
    std::cout << "Hulk Mod Tool: extracting the game's files to " << out.string() << "\n";
    std::ofstream index(out / "file list.txt", std::ios::binary);
    index << "path inside the archive\tarchive\tbytes\r\n";

    if (fs::exists(assets / "default.xbe", ec)) {
        std::cout << "From the extracted game in " << assets.string() << "\n";
        for (auto& e : fs::directory_iterator(assets, ec)) {
            if (!e.is_regular_file(ec) || e.path().extension() != ".rcf") continue;
            auto path = e.path();
            auto fp = std::shared_ptr<FILE>(_wfopen(path.c_str(), L"rb"), [](FILE* f) { if (f) std::fclose(f); });
            if (!fp) continue;
            Source src{path.filename().string(), uint64_t(fs::file_size(path, ec)),
                       [fp](uint64_t at, void* dst, size_t n) {
                           _fseeki64(fp.get(), int64_t(at), SEEK_SET);
                           return std::fread(dst, 1, n, fp.get());
                       }};
            Extract(src, out, index);
        }
    } else {
        std::vector<fs::path> isos;
        for (auto& e : fs::directory_iterator(assets, ec)) {
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext == L".iso") isos.push_back(e.path());
        }
        std::sort(isos.begin(), isos.end());
        kernel::Disc disc;
        std::string error;
        bool open = false;
        for (const fs::path& iso : isos)
            if ((open = disc.Open(iso, error))) {
                std::cout << "From the disc image " << iso.filename().string() << "\n";
                break;
            }
        if (!open) {
            std::cout << "No game in " << assets.string() << ": put your disc image (.iso) there first.\n";
            std::cin.get();
            return 1;
        }
        const kernel::Disc::Entry* root = disc.Find(std::vector<std::string>{});
        for (const kernel::Disc::Entry& e : root->children) {
            if (e.dir || e.name.size() < 4 || _stricmp(e.name.c_str() + e.name.size() - 4, ".rcf") != 0) continue;
            Source src{e.name, e.size, [&disc, &e](uint64_t at, void* dst, size_t n) -> size_t {
                           size_t total = 0;
                           while (total < n) {
                               uint32_t chunk = uint32_t(std::min<size_t>(n - total, 0x10000000));
                               uint32_t got = disc.Read(e, at + total, static_cast<uint8_t*>(dst) + total, chunk);
                               total += got;
                               if (got < chunk) break;
                           }
                           return total;
                       }};
            Extract(src, out, index);
        }
    }
    std::cout << "\nDone: " << g_written << " files written, " << g_kept << " already there";
    if (g_failed) std::cout << ", " << g_failed << " failed";
    std::cout << ".\nCopy a file into mods\\<your mod name>\\ with the same path, edit it, and start the game.\n"
              << "Press Enter to close.\n";
    if (argc <= 1) std::cin.get();
    return g_failed ? 1 : 0;
}
