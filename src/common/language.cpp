#include "common/language.h"
#include <atomic>
#include <cctype>
#include <cstring>
#include <initializer_list>

namespace hulk {
namespace {
std::atomic<uint32_t> g_language{uint32_t(Language::English)};

bool SameNoCase(const char* a, const char* b) {
    for (; *a && *b; a++, b++)
        if (std::tolower(uint8_t(*a)) != std::tolower(uint8_t(*b))) return false;
    return *a == *b;
}
}  // namespace

void SetLanguage(Language language) { g_language.store(uint32_t(language)); }
Language GetLanguage() { return Language(g_language.load()); }

const char* LanguageName(Language language) {
    switch (language) {
        case Language::French: return "french";
        case Language::Spanish: return "spanish";
        case Language::Italian: return "italian";
        default: return "english";
    }
}

bool LanguageFromName(const char* name, Language& out) {
    for (Language l : {Language::English, Language::French, Language::Spanish, Language::Italian})
        if (SameNoCase(name, LanguageName(l))) {
            out = l;
            return true;
        }
    return false;
}

}  // namespace hulk
