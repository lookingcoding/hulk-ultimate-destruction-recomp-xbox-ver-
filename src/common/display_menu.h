#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

struct X86Context;
namespace app { struct FileView; }

// The PC display settings as a screen of the game's own frontend. The game's menus are Radical "Dolly" FE scripts
// in misc.rcf (frontend\fe_real.txt the main menu, frontend\fe_run.txt the pause menu) with string tables beside
// them (frontend\lang\str_*.txt). The game reads a misc.rcf whose directory points those files at edited copies past
// the archive's end: each Options screen gets a Display Options row, which opens a screen of text selectors bound to
// named values; the game calls back the host when one changes. Nothing on disk changes.
namespace hulk {

// GameInfo::file: misc.rcf as the game reads it, null for every other file
std::shared_ptr<app::FileView> GameFileView(const std::string& path, uint64_t size,
                                            const std::function<size_t(uint64_t, void*, size_t)>& read);

// the button prompts the game reads from then on: a controller's icons, else the keyboard and mouse bindings. The
// game reads its string tables as it loads, so a change shows from the next load
void SetControllerPrompts(bool controller);
// which device's prompts to show: Automatic (a connected controller's, else the keyboard's), the keyboard's whatever
// is plugged in, or a controller's. settings.toml's button_prompts
enum class PromptDevice { Auto, Keyboard, Controller };
void SetPromptDevice(PromptDevice device);
PromptDevice GetPromptDevice();
const char* PromptDeviceName(PromptDevice device);  // "auto", "keyboard", "controller"
// the frontend's bottom prompt's button icons (owner: the frontend), set as those prompts are, before each is drawn:
// these show a change at once (hooks.cpp)
void MenuButtonPrompts(uint32_t owner);

// registers the frontend callbacks of the selectors, after the game's own (hooks.cpp)
void RegisterDisplayCallbacks(X86Context& ctx);
// a frontend int callback through the shared handler: true when it was one of the display selectors
bool DisplayCallback(uint32_t owner, uint32_t value);
// a frontend function call through the shared handler: true when it was the screen's, which sets each selector to
// the current setting as the game's own code does for its screens
bool DisplaySync(X86Context& ctx, uint32_t owner);
// applies settings the selectors changed once they settle, each polled pad
void TickDisplaySettings();

}  // namespace hulk
