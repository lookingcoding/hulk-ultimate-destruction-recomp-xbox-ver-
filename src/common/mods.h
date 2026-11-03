#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace app { struct FileView; }

// Mods: the mods folder beside assets holds a folder for each mod, and every file in one stands in for the game's
// file of the same path. A path inside an archive (art\copyright-en.p3d, frontend\fe_real.txt, scripts) replaces that
// file inside whichever archive holds it; a path of the disc's own (misc.rcf) replaces the whole file. Mods load in
// name order and a later one wins; a folder whose name starts with _ or . is off. Nothing on disk changes: the game
// reads a view of each archive whose directory points the replaced files past its end.
namespace hulk {

// finds the mods, before the game opens its files (game_info.cpp)
void LoadMods();

// GameInfo::file: what the game reads of a file it opens, mods and the Display Options screen (display_menu.h)
// applied; null when nothing changes
std::shared_ptr<app::FileView> ModFileView(const std::string& path, uint64_t size,
                                           const std::function<size_t(uint64_t, void*, size_t)>& read);

}  // namespace hulk
