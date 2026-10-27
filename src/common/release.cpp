#include "common/release.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "app/app.h"
#include "core/log.h"
#include "kernel/disc.h"

extern const char kImageSha1[];  // the xbe recompiled into this executable, written by CMake

namespace hulk {
namespace {

namespace fs = std::filesystem;

// each release's default.xbe and the executable recompiled from it, beside this one
struct Release {
    const char* sha1;
    const wchar_t* exe;
};
const Release kReleases[] = {
    {"8e4f5b90c59f10ffb8094862f7f6e6a7a7d76fd0", L"HulkRecompiled.exe"},     // title 56550039, entry 0020AAC6
    {"d5e7da38af1c3acbf6babe2b11a3e4c849a33362", L"HulkRecompiledUSA.exe"},  // title 56550039, entry 0020A016
};

// assets beside the exe or in a folder above it, as the runtime finds them (app/game_files.cpp)
fs::path FindAssets() {
    fs::path dir = app::ExeDir();
    std::error_code ec;
    for (int i = 0; i < 6; i++) {
        if (fs::is_directory(dir / "assets", ec)) return dir / "assets";
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return {};
}

// the release of the first copy of the game the runtime would consider, the extracted files before any image, or
// null when none is one of them
const Release* FirstRelease(const fs::path& assets) {
    auto known = [](const std::vector<uint8_t>& xbe) -> const Release* {
        std::string sha = app::Sha1(xbe);
        for (const Release& r : kReleases)
            if (sha == r.sha1) return &r;
        return nullptr;
    };
    std::vector<uint8_t> xbe;
    {
        std::ifstream f(assets / "default.xbe", std::ios::binary);
        if (f) {
            xbe.assign(std::istreambuf_iterator<char>(f), {});
            if (const Release* r = known(xbe)) return r;
        }
    }
    std::vector<fs::path> isos;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(assets, ec)) {
        std::wstring ext = e.path().extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](wchar_t c) { return wchar_t(std::towlower(c)); });
        if (ext == L".iso" && e.is_regular_file(ec)) isos.push_back(e.path());
    }
    std::sort(isos.begin(), isos.end());
    for (const fs::path& iso : isos) {
        kernel::Disc disc;
        std::string error;
        if (!disc.Open(iso, error) || !disc.Load("default.xbe", xbe)) continue;
        if (const Release* r = known(xbe)) return r;
    }
    return nullptr;
}

}  // namespace

void RunMatchingRelease() {
    fs::path assets = FindAssets();
    if (assets.empty()) return;
    const Release* r = FirstRelease(assets);
    if (!r || std::string(r->sha1) == kImageSha1) return;
    fs::path exe = app::ExeDir() / r->exe;
    std::error_code ec;
    if (!fs::is_regular_file(exe, ec)) return;  // the runtime offers to start the game anyway

    // the same arguments, to the executable built from this game
    std::wstring command = L"\"" + exe.wstring() + L"\"";
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; i++) command += L" \"" + std::wstring(argv[i]) + L"\"";
        LocalFree(argv);
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring dir = app::ExeDir().wstring();
    if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        LOGW("release: starting %ls failed (%lu)", exe.c_str(), GetLastError());
        return;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    LOGI("release: the game in assets is %ls's, started it", r->exe);
    ExitProcess(0);
}

}  // namespace hulk
