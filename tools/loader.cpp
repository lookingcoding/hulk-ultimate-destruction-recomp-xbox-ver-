// Hulk Recompiled.exe, the loader players start. The game's executables, one per disc of the game, are in game\ with
// their compiler, settings and saves; the loader starts the first, which hands over to the one built for the disc in
// assets (src/common/release.cpp) and finds assets in the folder above its own.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace {

bool IsFile(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    std::wstring dir(MAX_PATH, L'\0');
    dir.resize(GetModuleFileNameW(nullptr, dir.data(), DWORD(dir.size())));
    dir.resize(dir.find_last_of(L"\\/"));

    // game\ in a release; beside the loader in the build output
    std::wstring game = dir + L"\\game";
    if (!IsFile(game + L"\\HulkRecompiled.exe")) game = dir;
    std::wstring exe = game + L"\\HulkRecompiled.exe";

    std::wstring command = L"\"" + exe + L"\"";
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; i++) command += L" \"" + std::wstring(argv[i]) + L"\"";
        LocalFree(argv);
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!IsFile(exe) ||
        !CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, game.c_str(), &si, &pi)) {
        MessageBoxW(nullptr, L"The game folder is missing files. Extract the whole download again, then start the game.",
                    L"Hulk Recompiled", MB_ICONERROR);
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
