#pragma once
#include <cstdint>

// The game's language as a PC setting (settings.toml's language, a row of Display Options). An Xbox game reads the
// language from the console's settings: XGetLanguage (addresses.h, HULK_SUB_GET_LANGUAGE) returns the dashboard's
// choice, which the game turns into its own at boot (0x000B5296 on the European disc: English, French, Italian,
// Spanish, with the UK's English on a PAL console; any other language plays English). The runtime's console always
// says English, so hooks.cpp answers with this setting instead. The game reads it once, so a change shows from the next
// start.
namespace hulk {

// XC_LANGUAGE values
enum class Language : uint32_t { English = 1, French = 4, Spanish = 5, Italian = 6 };

void SetLanguage(Language language);
Language GetLanguage();
const char* LanguageName(Language language);       // "english", "french", "spanish", "italian"
bool LanguageFromName(const char* name, Language& out);  // any case

}  // namespace hulk
