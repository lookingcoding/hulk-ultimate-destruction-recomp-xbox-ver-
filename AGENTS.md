# Project instructions

## Native game menus

User-facing game settings and menus must use the game's native Dolly frontend, following `src/common/display_menu.cpp` and the original Display/Audio menu widgets. Use the same native fonts, art, navigation, Options entry pattern, selectors/sliders and dialogs. Never add an external, ImGui, overlay, or custom-drawn replacement menu. Test images must come from the real game renderer; component renders and mockups are not game screenshots.

All task files, builds, temporary files and caches stay on D:. Background game verification must stay hidden, use internal input only and disable audio output before initialization. Never show or activate a test window or move/clip the user's pointer.
