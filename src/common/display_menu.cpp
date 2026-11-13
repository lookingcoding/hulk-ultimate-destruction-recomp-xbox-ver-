#include "common/display_menu.h"
#include "common/addresses.h"
#include "common/cheats.h"
#include "common/language.h"
#include "common/rcf.h"
#include "common/view.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include "app/app.h"
#include "core/log.h"
#include "kernel/kernel.h"

namespace hulk {
namespace {

// default.xbe (addresses.h)
using addr::kGuestNew;             // operator new, cdecl
using addr::kRegisterIntCallback;  // cdecl (name, functor): keeps the name's hash and the functor
using addr::kIntFunctorVtable;     // functor: vtable, owner, method (thiscall owner, int value)
using addr::kSharedHandler;        // AudioQualityChanged, PS2 and GameCube only: hooks.cpp
using addr::kRegisterCallback;     // cdecl (name, functor): a function the scripts call
using addr::kFunctorVtable;        // functor: method (thiscall owner, argc, argv) returning int
using addr::kSyncHandler;          // Print, which no Xbox script calls: hooks.cpp
// a screen's selectors start where the game's code sets them (the European camera options at 0x000B5C50 do so):
using addr::kFindWidget;           // cdecl (name): the widget of the current screen
using addr::kDynamicCast;          // __RTDynamicCast (object, 0, from, to, 0)
using addr::kWidgetType;
using addr::kTextSelectorType;
constexpr uint32_t kSetSelection = 0x64;  // TextSelector vtable offset, thiscall (index)
// the selectors' functors carry these as owners: the guest's null page, never an object; kSettings is the sync
constexpr uint32_t kOwnerBase = 0x100;
constexpr const char* kSyncName = "PCDisplaySync";

constexpr auto kSettleTime = std::chrono::milliseconds(400);

// kFov and kRenderDistance are the game's view (view.h), applied at once; kLanguage the game's language
// (language.h), from the next start; the others are the window's, applied once they settle. The camera distance has
// no row: the mouse wheel sets it in play (view.h)
enum Setting { kMode, kResolution, kFrameRate, kVSync, kScale, kFov, kRenderDistance, kLanguage, kSettings };
// rows the window's settings don't hold
bool InGame(int s) { return s == kFov || s == kRenderDistance || s == kLanguage; }

struct Entry {
    const char* key;  // string table key of its text
    uint32_t a, b;    // the setting's value: mode, width and height, fps cap, vsync, percent, degrees, percent
};

struct Option {
    const char* named_value;  // the FE's named value, its callback is this + "Changed"
    const char* selector;     // the selector's name in the screen
    const char* label;        // string table key
    std::vector<Entry> entries;
};

// a string table line in each language: en and uk, fr, es, it
struct Text {
    const char* key;
    const char* text[4];
};

const Text kTexts[] = {
    {"TBS_PC_DISPLAY_OPTIONS", {"Display Options", "Options d'affichage", "Opciones de pantalla", "Opzioni schermo"}},
    {"TBS_PC_DISPLAY_MODE", {"Display Mode", "Mode d'affichage", "Modo de pantalla", "Modalità schermo"}},
    {"TBS_PC_FULLSCREEN", {"Fullscreen", "Plein écran", "Pantalla completa", "Schermo intero"}},
    {"TBS_PC_BORDERLESS", {"Borderless", "Fenêtre sans bordure", "Ventana sin bordes", "Finestra senza bordi"}},
    {"TBS_PC_WINDOWED", {"Windowed", "Fenêtré", "Ventana", "Finestra"}},
    {"TBS_PC_RESOLUTION", {"Resolution", "Résolution", "Resolución", "Risoluzione"}},
    {"TBS_PC_RES_DESKTOP", {"Desktop", "Bureau", "Escritorio", "Desktop"}},
    {"TBS_PC_RES_1280X720", {"1280 x 720", "1280 x 720", "1280 x 720", "1280 x 720"}},
    {"TBS_PC_RES_1600X900", {"1600 x 900", "1600 x 900", "1600 x 900", "1600 x 900"}},
    {"TBS_PC_RES_1920X1080", {"1920 x 1080", "1920 x 1080", "1920 x 1080", "1920 x 1080"}},
    {"TBS_PC_RES_2560X1440", {"2560 x 1440", "2560 x 1440", "2560 x 1440", "2560 x 1440"}},
    {"TBS_PC_RES_3840X2160", {"3840 x 2160", "3840 x 2160", "3840 x 2160", "3840 x 2160"}},
    {"TBS_PC_FRAME_RATE", {"Frame Rate Limit", "Limite d'images", "Límite de fotogramas", "Limite fotogrammi"}},
    {"TBS_PC_FPS_30", {"30", "30", "30", "30"}},
    {"TBS_PC_FPS_60", {"60", "60", "60", "60"}},
    {"TBS_PC_FPS_120", {"120", "120", "120", "120"}},
    {"TBS_PC_FPS_144", {"144", "144", "144", "144"}},
    {"TBS_PC_FPS_REFRESH", {"Refresh Rate", "Fréquence de l'écran", "Frecuencia de pantalla", "Frequenza schermo"}},
    {"TBS_PC_FPS_UNLIMITED", {"Unlimited", "Illimitée", "Ilimitado", "Illimitato"}},
    {"TBS_PC_VSYNC", {"V-Sync", "Synchro verticale", "Sincronización vertical", "Sincronia verticale"}},
    {"TBS_PC_RENDER_SCALE", {"Render Scale", "Échelle de rendu", "Escala de renderizado", "Scala di rendering"}},
    {"TBS_PC_SCALE_50", {"50%", "50 %", "50%", "50%"}},
    {"TBS_PC_SCALE_75", {"75%", "75 %", "75%", "75%"}},
    {"TBS_PC_SCALE_100", {"100%", "100 %", "100%", "100%"}},
    {"TBS_PC_SCALE_150", {"150%", "150 %", "150%", "150%"}},
    {"TBS_PC_SCALE_200", {"200%", "200 %", "200%", "200%"}},
    {"TBS_PC_FOV", {"Field of View", "Champ de vision", "Campo de visión", "Campo visivo"}},
    {"TBS_PC_DEFAULT", {"Default", "Par défaut", "Predeterminado", "Predefinito"}},
    {"TBS_PC_FOV_5", {"+5", "+5", "+5", "+5"}},
    {"TBS_PC_FOV_10", {"+10", "+10", "+10", "+10"}},
    {"TBS_PC_FOV_15", {"+15", "+15", "+15", "+15"}},
    {"TBS_PC_FOV_20", {"+20", "+20", "+20", "+20"}},
    {"TBS_PC_FOV_30", {"+30", "+30", "+30", "+30"}},
    {"TBS_PC_RENDER_DISTANCE", {"Render Distance", "Distance d'affichage", "Distancia de dibujado", "Distanza visiva"}},
    {"TBS_PC_SCALE_300", {"300%", "300 %", "300%", "300%"}},
    {"TBS_PC_SCALE_400", {"400%", "400 %", "400%", "400%"}},
    {"TBS_PC_CUSTOM", {"Custom", "Personnalisé", "Personalizado", "Personalizzato"}},
    {"TBS_PC_LANGUAGE", {"Language (restart)", "Langue (redémarrage)", "Idioma (reiniciar)", "Lingua (riavvio)"}},
    {"TBS_PC_LANG_EN", {"English", "English", "English", "English"}},
    {"TBS_PC_LANG_FR", {"Français", "Français", "Français", "Français"}},
    {"TBS_PC_LANG_ES", {"Español", "Español", "Español", "Español"}},
    {"TBS_PC_LANG_IT", {"Italiano", "Italiano", "Italiano", "Italiano"}},
    // Options > Cheats (cheats.h); its title, Invulnerability, Low Gravity, On, Off, Normal and Code Input are the
    // game's own lines
    {"TBS_PC_CHEAT_INFINITE_RAGE", {"Infinite Rage", "Rage infinie", "Furia infinita", "Rabbia infinita"}},
    {"TBS_PC_CHEAT_INFINITE_HEALTH", {"Infinite Health", "Santé infinie", "Salud infinita", "Salute infinita"}},
    {"TBS_PC_CHEAT_REGENERATION", {"Fast Regeneration", "Régénération rapide", "Regeneración rápida", "Rigenerazione rapida"}},
    {"TBS_PC_CHEAT_DAMAGE", {"Hulk Damage", "Dégâts de Hulk", "Daño de Hulk", "Danni di Hulk"}},
    {"TBS_PC_DAMAGE_X2", {"x2", "x2", "x2", "x2"}},
    {"TBS_PC_DAMAGE_X4", {"x4", "x4", "x4", "x4"}},
    {"TBS_PC_DAMAGE_ONE_HIT", {"One-Hit Kills", "Coup fatal", "Muerte de un golpe", "Colpo mortale"}},
    {"TBS_PC_CHEAT_GAME_SPEED", {"Game Speed", "Vitesse du jeu", "Velocidad del juego", "Velocità di gioco"}},
    {"TBS_PC_SCALE_25", {"25%", "25 %", "25%", "25%"}},
    {"TBS_PC_CHEAT_SUPER_JUMP", {"Super Jump", "Super saut", "Supersalto", "Super salto"}},
    {"TBS_PC_CHEAT_SUPER_SPEED", {"Super Speed", "Super vitesse", "Supervelocidad", "Super velocità"}},
    {"TBS_PC_CHEATS_MOVEMENT", {"Movement Cheats", "Triches de mouvement", "Trucos de movimiento", "Cheat di movimento"}},
};

// Options > Cheats: a selector per cheat, its choices in level order (level 0 is off), on two screens; the main menu's
// first screen also leads to the game's own Code Input screen
using cheats::Cheat;
struct CheatRow {
    Cheat cheat;
    const char* name;   // the named value is "PCCheat" + this, its callback that + "Changed"
    const char* label;  // string table key
    std::vector<const char*> choices;
    int screen;  // 0: Cheats, 1: Movement Cheats
};
const std::vector<const char*> kOffOn = {"TBS_MENU_ITEM_CHOICE_OFF", "TBS_MENU_ITEM_CHOICE_ON"};
const CheatRow kCheatRows[] = {
    {Cheat::Invulnerability, "Invulnerability", "TBS_CHEAT_INVULNERABILITY", kOffOn, 0},
    {Cheat::InfiniteRage, "InfiniteRage", "TBS_PC_CHEAT_INFINITE_RAGE", kOffOn, 0},
    {Cheat::InfiniteHealth, "InfiniteHealth", "TBS_PC_CHEAT_INFINITE_HEALTH", kOffOn, 0},
    {Cheat::Regeneration, "Regeneration", "TBS_PC_CHEAT_REGENERATION", kOffOn, 0},
    {Cheat::Damage, "Damage", "TBS_PC_CHEAT_DAMAGE",
     {"TBS_CHEAT_RENDER_MODE_NORMAL", "TBS_PC_DAMAGE_X2", "TBS_PC_DAMAGE_X4", "TBS_PC_DAMAGE_ONE_HIT"}, 0},
    {Cheat::GameSpeed, "GameSpeed", "TBS_PC_CHEAT_GAME_SPEED",
     {"TBS_PC_SCALE_100", "TBS_PC_SCALE_75", "TBS_PC_SCALE_50", "TBS_PC_SCALE_25"}, 0},
    {Cheat::SuperJump, "SuperJump", "TBS_PC_CHEAT_SUPER_JUMP", kOffOn, 1},
    {Cheat::LowGravity, "LowGravity", "TBS_CHEAT_LOW_GRAVITY", kOffOn, 1},
    {Cheat::SuperSpeed, "SuperSpeed", "TBS_PC_CHEAT_SUPER_SPEED", kOffOn, 1},
};
constexpr int kCheatRowCount = int(std::size(kCheatRows));
constexpr const char* kCheatScreens[] = {"pc_cheats", "pc_cheats_movement"};
constexpr const char* kCheatSyncName = "PCCheatSync";
// the cheats' functors' owners, past the display options' (kSettings and its sync)
constexpr uint32_t kCheatSyncOwner = 0x11F, kCheatOwnerBase = 0x120;
std::string CheatSelector(const CheatRow& r) { return std::string("pc_cheat_") + r.name + "_selector"; }

// the string tables and the column of kTexts each takes
const std::pair<const char*, int> kLanguages[] = {
    {"frontend\\lang\\str_en.txt", 0}, {"frontend\\lang\\str_uk.txt", 0}, {"frontend\\lang\\str_fr.txt", 1},
    {"frontend\\lang\\str_es.txt", 2}, {"frontend\\lang\\str_it.txt", 3},
};

bool Matches(Setting s, const Entry& e, const app::Settings& st) {
    switch (s) {
        case kMode: return uint32_t(st.display_mode) == e.a;
        case kResolution: return st.width == e.a && st.height == e.b;
        case kFrameRate: return st.fps_cap == e.a;
        case kVSync: return uint32_t(st.vsync) == e.a;
        case kScale: return st.render_percent == e.a;
        case kFov: return uint32_t(GetFovIncrease()) == e.a;
        case kRenderDistance: return uint32_t(GetRenderDistance()) == e.a;
        case kLanguage: return uint32_t(GetLanguage()) == e.a;
        default: return false;
    }
}

void Assign(Setting s, const Entry& e, app::Settings& st) {
    switch (s) {
        case kMode: st.display_mode = app::DisplayMode(e.a); break;
        case kResolution: st.width = e.a, st.height = e.b; break;
        case kFrameRate: st.fps_cap = e.a; break;
        case kVSync: st.vsync = e.a != 0; break;
        case kScale: st.render_percent = e.a; break;
        case kFov: SetFovIncrease(int(e.a)); break;
        case kRenderDistance: SetRenderDistance(int(e.a)); break;
        case kLanguage: SetLanguage(Language(e.a)); break;
        default: break;
    }
}

// the selectors' choices; a value the settings file holds outside them is kept as Custom
std::vector<Option> MakeOptions(const app::Settings& st) {
    std::vector<Option> o = {
        {"PCDisplayMode", "pc_display_mode_selector", "TBS_PC_DISPLAY_MODE",
         {{"TBS_PC_FULLSCREEN", uint32_t(app::DisplayMode::Fullscreen), 0},
          {"TBS_PC_BORDERLESS", uint32_t(app::DisplayMode::Borderless), 0},
          {"TBS_PC_WINDOWED", uint32_t(app::DisplayMode::Windowed), 0}}},
        {"PCResolution", "pc_resolution_selector", "TBS_PC_RESOLUTION",
         {{"TBS_PC_RES_DESKTOP", 0, 0},
          {"TBS_PC_RES_1280X720", 1280, 720},
          {"TBS_PC_RES_1600X900", 1600, 900},
          {"TBS_PC_RES_1920X1080", 1920, 1080},
          {"TBS_PC_RES_2560X1440", 2560, 1440},
          {"TBS_PC_RES_3840X2160", 3840, 2160}}},
        {"PCFrameRate", "pc_frame_rate_selector", "TBS_PC_FRAME_RATE",
         {{"TBS_PC_FPS_30", 30, 0},
          {"TBS_PC_FPS_60", 60, 0},
          {"TBS_PC_FPS_120", 120, 0},
          {"TBS_PC_FPS_144", 144, 0},
          {"TBS_PC_FPS_REFRESH", app::kFpsCapRefresh, 0},
          {"TBS_PC_FPS_UNLIMITED", 0, 0}}},
        {"PCVSync", "pc_vsync_selector", "TBS_PC_VSYNC",
         {{"TBS_MENU_ITEM_CHOICE_ON", 1, 0}, {"TBS_MENU_ITEM_CHOICE_OFF", 0, 0}}},
        {"PCRenderScale", "pc_render_scale_selector", "TBS_PC_RENDER_SCALE",
         {{"TBS_PC_SCALE_50", 50, 0},
          {"TBS_PC_SCALE_75", 75, 0},
          {"TBS_PC_SCALE_100", 100, 0},
          {"TBS_PC_SCALE_150", 150, 0},
          {"TBS_PC_SCALE_200", 200, 0}}},
        {"PCFieldOfView", "pc_fov_selector", "TBS_PC_FOV",
         {{"TBS_PC_DEFAULT", 0, 0},
          {"TBS_PC_FOV_5", 5, 0},
          {"TBS_PC_FOV_10", 10, 0},
          {"TBS_PC_FOV_15", 15, 0},
          {"TBS_PC_FOV_20", 20, 0},
          {"TBS_PC_FOV_30", 30, 0}}},
        {"PCRenderDistance", "pc_render_distance_selector", "TBS_PC_RENDER_DISTANCE",
         {{"TBS_PC_DEFAULT", 100, 0},
          {"TBS_PC_SCALE_150", 150, 0},
          {"TBS_PC_SCALE_200", 200, 0},
          {"TBS_PC_SCALE_300", 300, 0},
          {"TBS_PC_SCALE_400", 400, 0}}},
        {"PCLanguage", "pc_language_selector", "TBS_PC_LANGUAGE",
         {{"TBS_PC_LANG_EN", uint32_t(Language::English), 0},
          {"TBS_PC_LANG_FR", uint32_t(Language::French), 0},
          {"TBS_PC_LANG_ES", uint32_t(Language::Spanish), 0},
          {"TBS_PC_LANG_IT", uint32_t(Language::Italian), 0}}},
    };
    for (int s = 0; s < kSettings; s++) {
        auto& entries = o[s].entries;
        if (std::none_of(entries.begin(), entries.end(), [&](const Entry& e) { return Matches(Setting(s), e, st); })) {
            Entry custom{"TBS_PC_CUSTOM", 0, 0};
            switch (s) {
                case kMode: custom.a = uint32_t(st.display_mode); break;
                case kResolution: custom.a = st.width, custom.b = st.height; break;
                case kFrameRate: custom.a = st.fps_cap; break;
                case kVSync: custom.a = st.vsync; break;
                case kScale: custom.a = st.render_percent; break;
                case kFov: custom.a = uint32_t(GetFovIncrease()); break;
                case kRenderDistance: custom.a = uint32_t(GetRenderDistance()); break;
                case kLanguage: custom.a = uint32_t(GetLanguage()); break;
            }
            entries.push_back(custom);
        }
    }
    return o;
}

std::mutex g_lock;
std::vector<Option> g_options;  // made once when misc.rcf is first opened, before the frontend loads
std::atomic<bool> g_ready{false};   // g_options is made and stays as it is
std::atomic<bool> g_served{false};  // the game reads the edited script
std::atomic<bool> g_controller_prompts{false};  // the string tables as the game reads them: controller or keyboard
std::atomic<int> g_prompt_device{0};  // PromptDevice: automatic unless settings.toml says otherwise
std::atomic<int> g_language{0};  // the column of the string table the game read last: its language
std::atomic<int64_t> g_changed_at{0};  // steady clock ticks of the last unsaved change, 0 for none
std::atomic<bool> g_display_changed{false};  // a window setting changed since the last apply

// the selection a selector starts at: the current setting's (one digit, the script's bytes keep their length)
char Selection(Setting s) {
    const auto& st = app::Get();
    const auto& entries = g_options[s].entries;
    for (size_t i = 0; i < entries.size(); i++)
        if (Matches(s, entries[i], st)) return char('0' + i);
    return '0';
}

// where a frontend script puts its rows: the main menu (fe_real.txt) or the pause menu (fe_run.txt)
struct Layout {
    const char* file;
    const char* item;   // the Options screen row after Camera Options
    const char* cheats_item;  // and the one after it, Cheats
    const char* title;  // the screen title label
    const char* row;    // the screen rows, numbered from 1
    bool main;          // the main menu formats rows with its menu_item_N macros, the pause menu with select bars
};
const Layout kLayouts[] = {
    {"frontend\\fe_real.txt", "FEmain_left_item5", "FEmain_left_item6", "FEsubMenu_title", "FEsubMenu_text", true},  // FESlotName in feconfig.txt
    {"frontend\\fe_run.txt", "FEpause_text4", "FEpause_text5", "FEpause_title", "FEpause_text", false},
};

// a script, edited: a row after Camera Options in its Options screen, the selectors' named values and the
// Display Options screen before its camera options screen
struct Script {
    std::string text;
    std::vector<size_t> selection;  // offsets of the named values' digits: the display options', then the cheats'
};

// the digit a named value starts at: a display option's setting, then each cheat's level
char Digit(int i) {
    if (i < kSettings) return Selection(Setting(i));
    return char('0' + std::clamp(cheats::Level(kCheatRows[i - kSettings].cheat), 0, 9));
}

// Camera Options' Invert X and Invert Y name their choices the other way round: the game shows "Yes" for the camera
// that looks up as the stick goes up, the one players expect, and plays "No" upside down (camera.h). Each selector's
// choices swap places, the same length, so "No" is that camera
void SwapInvertLabels(std::string& script) {
    const std::string yes_no = "TextSelection \"\" TBS_MENU_ITEM_YES TBS_MENU_ITEM_NO";
    const std::string no_yes = "TextSelection \"\" TBS_MENU_ITEM_NO TBS_MENU_ITEM_YES";
    for (const char* value : {"NamedValue \"CameraInvertX\"", "NamedValue \"CameraInvertY\""}) {
        for (size_t at = script.find(value); at != std::string::npos; at = script.find(value, at + 1)) {
            size_t choices = script.rfind(yes_no, at);
            if (choices != std::string::npos && at - choices < 120) script.replace(choices, yes_no.size(), no_yes);
        }
    }
}

std::optional<Script> EditScript(const std::string& in, const Layout& l) {
    const std::string nl = in.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    const std::string options_screen = "AddScreen \"options\"";
    const std::string camera_row = "OnSelect gotoscreen \"camera_options\"" + nl;
    const std::string row_end = "    }" + nl;
    const std::string screen_at = "AddScreen \"camera_options\"";

    size_t options = in.find(options_screen);
    size_t camera = options == std::string::npos ? options : in.find(camera_row, options);
    size_t row = camera == std::string::npos ? camera : in.find(row_end, camera);
    size_t screen = in.find(screen_at);
    if (row == std::string::npos || screen == std::string::npos || row > screen) {
        LOGW("display menu: %s is not the script it expects, its Options stays as it is", l.file);
        return std::nullopt;
    }
    row += row_end.size();
    auto bars = [&](const std::string& n) {
        if (l.main) return "        $MACRO_INSERT menu_item_" + n + nl;
        return nl + "        SelectedDrawable \"FE_selectBar\"" + nl + "        NotSelectedDrawable \"FE_backgroundBar\"" + nl + nl +
               "        TransitionAnimEnter \"slide_down" + n + "\"" + nl + "        TransitionAnimExit  \"slide_down" + n + "\"" + nl;
    };

    // the Options screen's rows after Camera Options: Display Options, then Cheats
    auto options_row = [&](const char* slot, const char* text, const char* screen_name, const std::string& n) {
        std::string row_text;
        row_text += nl;
        row_text += std::string("    AddTextButton \"") + slot + "\"" + nl;
        row_text += "    {" + nl;
        row_text += std::string("        text \"") + text + "\" 1" + nl;
        row_text += std::string("        OnSelect gotoscreen \"") + screen_name + "\"" + nl;
        row_text += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"1\"" + nl;
        if (l.main) row_text += bars(n) + "        halign left" + nl;
        else row_text += nl + "        $MACRO_INSERT FormatText" + nl + bars(n);
        row_text += "    }" + nl;
        return row_text;
    };
    std::string item = options_row(l.item, "TBS_PC_DISPLAY_OPTIONS", "display_options", l.main ? "5" : "4") +
                       options_row(l.cheats_item, "TBS_MENU_ITEM_SF_CHEATS", kCheatScreens[0], l.main ? "6" : "5");

    std::string sub;
    sub += "# PC display options (Hulk Recompiled): the selectors call back the host" + nl;
    std::vector<size_t> digits;  // within sub
    for (int s = 0; s < kSettings; s++) {
        sub += std::string("AddNamedValueInt \"") + g_options[s].named_value + "\" ";
        digits.push_back(sub.size());
        sub += Selection(Setting(s));
        sub += std::string(" \"") + g_options[s].named_value + "Changed\"" + nl;
    }
    sub += nl + "AddScreen \"display_options\"" + nl;
    sub += "{" + nl;
    sub += "    $MACRO_INSERT typicalScreenSetup" + nl + nl;
    sub += std::string("    AddLabel \"") + l.title + "\"" + nl;
    sub += "    {" + nl;
    sub += "        text \"TBS_PC_DISPLAY_OPTIONS\" 1" + nl;
    sub += "        $MACRO_INSERT FormatTitleText" + nl + nl;
    sub += "        TransitionAnimEnter \"fadein\"" + nl;
    sub += "        TransitionAnimExit  \"fadeout\"" + nl;
    sub += "    }" + nl;
    for (int s = 0; s < kSettings; s++) {
        const Option& o = g_options[s];
        std::string row_n = std::to_string(s + 1);
        sub += nl + "    AddTextSelector \"" + l.row + row_n + "\"" + nl;
        sub += "    {" + nl;
        sub += std::string("        text \"") + o.label + "\" 1" + nl + nl;
        sub += std::string("        name \"") + o.selector + "\"" + nl;
        sub += "        TextSelection \"\"";
        for (const Entry& e : o.entries) sub += std::string(" ") + e.key;
        sub += nl;
        sub += std::string("        NamedValue \"") + o.named_value + "\"" + nl + nl;
        sub += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"2\"" + nl;
        sub += std::string("        OnHighlight function \"") + kSyncName + "\"" + nl;
        sub += bars(row_n);
        sub += "    }" + nl;
    }
    sub += nl + "    $MACRO_INSERT SetupControlButtonLabel" + nl;
    sub += "}" + nl + nl;

    // Options > Cheats (cheats.h): the same kind of selectors, calling back the host with the cheat's level
    sub += "# Hulk Recompiled cheats: the selectors call back the host" + nl;
    for (int i = 0; i < kCheatRowCount; i++) {
        sub += std::string("AddNamedValueInt \"PCCheat") + kCheatRows[i].name + "\" ";
        digits.push_back(sub.size());
        sub += Digit(kSettings + i);
        sub += std::string(" \"PCCheat") + kCheatRows[i].name + "Changed\"" + nl;
    }
    for (int page = 0; page < int(std::size(kCheatScreens)); page++) {
        sub += nl + "AddScreen \"" + kCheatScreens[page] + "\"" + nl;
        sub += "{" + nl;
        sub += "    $MACRO_INSERT typicalScreenSetup" + nl + nl;
        sub += std::string("    AddLabel \"") + l.title + "\"" + nl;
        sub += "    {" + nl;
        sub += std::string("        text \"") + (page ? "TBS_PC_CHEATS_MOVEMENT" : "TBS_MENU_ITEM_SF_CHEATS") + "\" 1" + nl;
        sub += "        $MACRO_INSERT FormatTitleText" + nl + nl;
        sub += "        TransitionAnimEnter \"fadein\"" + nl;
        sub += "        TransitionAnimExit  \"fadeout\"" + nl;
        sub += "    }" + nl;
        int n = 0;
        for (const CheatRow& r : kCheatRows) {
            if (r.screen != page) continue;
            const std::string row_n = std::to_string(++n);
            sub += nl + "    AddTextSelector \"" + l.row + row_n + "\"" + nl;
            sub += "    {" + nl;
            sub += std::string("        text \"") + r.label + "\" 1" + nl + nl;
            sub += "        name \"" + CheatSelector(r) + "\"" + nl;
            sub += "        TextSelection \"\"";
            for (const char* c : r.choices) sub += std::string(" ") + c;
            sub += nl;
            sub += std::string("        NamedValue \"PCCheat") + r.name + "\"" + nl + nl;
            sub += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"2\"" + nl;
            sub += std::string("        OnHighlight function \"") + kCheatSyncName + "\"" + nl;
            sub += bars(row_n);
            sub += "    }" + nl;
        }
        auto button = [&](const char* text, const char* target) {
            const std::string row_n = std::to_string(++n);
            sub += nl + "    AddTextButton \"" + l.row + row_n + "\"" + nl;
            sub += "    {" + nl;
            sub += std::string("        text \"") + text + "\" 1" + nl + nl;
            sub += std::string("        OnSelect gotoscreen \"") + target + "\"" + nl;
            sub += "        OnHighlight function \"UpdateControlButtonString\" \"DynamicControlButtonLabel\"  \"1\"" + nl;
            sub += bars(row_n);
            sub += "    }" + nl;
        };
        if (page == 0) {
            button("TBS_PC_CHEATS_MOVEMENT", kCheatScreens[1]);
            if (l.main) button("TBS_MENU_ITEM_SF_CODE_INPUT", "cheats");  // the disc's own Code Input screen
        }
        sub += nl + "    $MACRO_INSERT SetupControlButtonLabel" + nl;
        sub += "}" + nl + nl;
    }

    Script out{};
    out.text = in.substr(0, row) + item + in.substr(row, screen - row) + sub + in.substr(screen);
    for (size_t d : digits) out.selection.push_back(screen + item.size() + d);
    return out;
}

void AppendUtf16(std::vector<uint8_t>& out, const char* utf8) {
    for (const uint8_t* p = reinterpret_cast<const uint8_t*>(utf8); *p;) {
        uint32_t c = *p++;
        if (c >= 0xE0) {
            c = (c & 0x0F) << 12 | (p[0] & 0x3F) << 6 | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0xC0) {
            c = (c & 0x1F) << 6 | (p[0] & 0x3F);
            p += 1;
        }
        out.push_back(uint8_t(c));
        out.push_back(uint8_t(c >> 8));
    }
}

// The keyboard and mouse binding of each controller button prompt. The strings name buttons as \b and two letters,
// which the font draws as the controller's button; keyboard.cpp maps the keys to those buttons.
struct Prompt {
    char code[3];
    const char* text[4];  // en and uk, fr, es, it
};
const Prompt kKeyboardPrompts[] = {
    {"ju", {"[Space]", "[Espace]", "[Espacio]", "[Spazio]"}},                                       // A
    {"us", {"[Q]", "[Q]", "[Q]", "[Q]"}},                                                         // B
    {"at", {"[Left Click]", "[Clic gauche]", "[Clic izquierdo]", "[Clic sinistro]"}},             // X
    {"sp", {"[E]", "[E]", "[E]", "[E]"}},                                                         // Y
    {"ta", {"[Right Click]", "[Clic droit]", "[Clic derecho]", "[Clic destro]"}},                 // left trigger
    {"ru", {"[Shift]", "[Maj]", "[Mayús]", "[Maiusc]"}},                                          // right trigger
    {"la", {"[WASD]", "[WASD]", "[WASD]", "[WASD]"}},                                             // left stick
    {"lr", {"[A/D]", "[A/D]", "[A/D]", "[A/D]"}},                                                 // left stick around
    {"ra", {"[Mouse]", "[Souris]", "[Ratón]", "[Mouse]"}},                                        // right stick
    {"se", {"[Space]", "[Espace]", "[Espacio]", "[Spazio]"}},                                       // A as select
    {"jc", {"[Hold Space]", "[Maintenir Espace]", "[Mantener Espacio]", "[Tenere Spazio]"}},      // A held
    {"uc", {"[Hold Q]", "[Maintenir Q]", "[Mantener Q]", "[Tenere Q]"}},                          // B held
    {"ac", {"[Hold Left Click]", "[Maintenir clic gauche]", "[Mantener clic izquierdo]", "[Tenere clic sinistro]"}},  // X held
    {"sc", {"[Hold E]", "[Maintenir E]", "[Mantener E]", "[Tenere E]"}},                          // Y held
};

// The same buttons in PlayStation (DualSense) wording, settings.toml's button_prompts = "playstation"
// (issue #11). The disc's font only draws the Xbox glyphs, so the shapes are named in text; the game's
// string format has no colour codes, so the words are not coloured.
const Prompt kPlaystationPrompts[] = {
    {"ju", {"[Cross]", "[Croix]", "[Cruz]", "[Croce]"}},
    {"us", {"[Circle]", "[Rond]", "[Círculo]", "[Cerchio]"}},
    {"at", {"[Square]", "[Carré]", "[Cuadrado]", "[Quadrato]"}},
    {"sp", {"[Triangle]", "[Triangle]", "[Triángulo]", "[Triangolo]"}},
    {"ta", {"[L2]", "[L2]", "[L2]", "[L2]"}},
    {"ru", {"[R2]", "[R2]", "[R2]", "[R2]"}},
    {"la", {"[Left Stick]", "[Stick gauche]", "[Stick izquierdo]", "[Stick sinistro]"}},
    {"lr", {"[Left Stick]", "[Stick gauche]", "[Stick izquierdo]", "[Stick sinistro]"}},
    {"ra", {"[Right Stick]", "[Stick droit]", "[Stick derecho]", "[Stick destro]"}},
    {"se", {"[Cross]", "[Croix]", "[Cruz]", "[Croce]"}},
    {"jc", {"[Hold Cross]", "[Maintenir Croix]", "[Mantener Cruz]", "[Tenere Croce]"}},
    {"uc", {"[Hold Circle]", "[Maintenir Rond]", "[Mantener Círculo]", "[Tenere Cerchio]"}},
    {"ac", {"[Hold Square]", "[Maintenir Carré]", "[Mantener Cuadrado]", "[Tenere Quadrato]"}},
    {"sc", {"[Hold Triangle]", "[Maintenir Triangle]", "[Mantener Triángulo]", "[Tenere Triangolo]"}},
};

// The frontend's bottom prompt (0x000BCA60) puts each label after its button's icon, text it keeps at owner + 0x1884
// + button * 0x80 (64 UTF-16 units) from "se", "ba", "de" in default.xbe (0x000C527B): select, back and
// delete, the menu's A, B and X; keyboard.cpp maps the keys to those buttons
constexpr uint32_t kMenuIcons = 0x1884, kMenuIconSize = 0x80;
const Prompt kMenuPrompts[] = {
    {"se", {"[Enter]", "[Entrée]", "[Intro]", "[Invio]"}},
    {"ba", {"[Q]", "[Q]", "[Q]", "[Q]"}},
    {"de", {"[Delete]", "[Suppr]", "[Supr]", "[Canc]"}},
};

// the same three in the PlayStation's wording, button_prompts = "playstation"
const Prompt kPlaystationMenuPrompts[] = {
    {"se", {"[Cross]", "[Croix]", "[Cruz]", "[Croce]"}},
    {"ba", {"[Circle]", "[Rond]", "[Círculo]", "[Cerchio]"}},
    {"de", {"[Square]", "[Carré]", "[Cuadrado]", "[Quadrato]"}},
};

// the game's lines that name a controller button in words rather than as a \b glyph: the keyboard's wording, from
// keyboard.cpp's bindings (A and select are Space or Enter, START is Escape, so Enter starts the title and the
// splash screen's "Press START" would name a button a keyboard has not got)
const Text kKeyboardLines[] = {
    {"TBS_MENU_ITEM_PRESS_START", {"[Space] Start", "[Espace] Start", "[Espacio] Start", "[Spazio] Start"}},
    {"TBS_MENU_ITEM_PRESS_START2", {"Press Enter", "Appuyez sur Entrée", "Pulse Intro", "Premi Invio"}},
    {"TBS_MENU_ITEM_PRESS_START2_XBOX", {"Press Enter", "Appuyez sur Entrée", "Pulse Intro", "Premi Invio"}},
    {"TBS_MENU_ITEM_PRESS_START2_GC", {"Press Enter", "Appuyez sur Entrée", "Pulse Intro", "Premi Invio"}},
    {"TBS_MENU_ITEM_PRESS_START2_PS2", {"Press Enter", "Appuyez sur Entrée", "Pulse Intro", "Premi Invio"}},
    {"TBS_MENU_ITEM_PRESS_START3", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_XBOX", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_GC", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_PS2", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
};

// The same lines in the PlayStation's wording, button_prompts = "playstation"
const Text kPlaystationLines[] = {
    {"TBS_MENU_ITEM_PRESS_START", {"[Cross] Start", "[Croix] Start", "[Cruz] Start", "[Croce] Start"}},
    {"TBS_MENU_ITEM_PRESS_START2", {"Press Cross", "Appuyez sur Croix", "Pulse Cruz", "Premi Croce"}},
    {"TBS_MENU_ITEM_PRESS_START2_XBOX", {"Press Cross", "Appuyez sur Croix", "Pulse Cruz", "Premi Croce"}},
    {"TBS_MENU_ITEM_PRESS_START2_GC", {"Press Cross", "Appuyez sur Croix", "Pulse Cruz", "Premi Croce"}},
    {"TBS_MENU_ITEM_PRESS_START2_PS2", {"Press Cross", "Appuyez sur Croix", "Pulse Cruz", "Premi Croce"}},
    {"TBS_MENU_ITEM_PRESS_START3", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_XBOX", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_GC", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
    {"TBS_MENU_ITEM_PRESS_START3_PS2", {"Press any key", "Appuyez sur une touche", "Pulse una tecla", "Premi un tasto"}},
};

bool KeyIs(const std::u16string& key, const char* name) {
    const size_t n = std::strlen(name);
    if (key.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (key[i] != char16_t(uint8_t(name[i]))) return false;
    return true;
}

bool StartsWith(const std::u16string& key, const char* prefix) {
    const size_t n = std::strlen(prefix);
    if (key.size() < n) return false;
    for (size_t i = 0; i < n; i++)
        if (key[i] != char16_t(uint8_t(prefix[i]))) return false;
    return true;
}

// The HUD's mission hints (hud_triggerButtonHintBegin in the mission scenarios) draw a controller button's icon
// and then the instruction text, which is one of these keys. Their scenarios use exactly three buttons: A
// ("\bse": acquire, enter, hint, jump, mission select), Y ("\bsp": break free, hammer toss, weaponize) and
// B ("\bus": drop); the icon is left out with the keyboard's prompts (hooks.cpp) and the key names the button
// first, here, in the instruction's own line.
const Text kHintKeys[] = {
    {"TBS_INSTRUCTION_BREAK_FREE", {"[E] ", "[E] ", "[E] ", "[E] "}},
    {"TBS_INSTRUCTION_HAMMERTOSS", {"[E] ", "[E] ", "[E] ", "[E] "}},
    {"TBS_INSTRUCTION_WEAPONIZE", {"[E] ", "[E] ", "[E] ", "[E] "}},
    {"TBS_INSTRUCTION_DROP", {"[Q] ", "[Q] ", "[Q] ", "[Q] "}},
};

// The same hints in the PlayStation's wording: the scenario's buttons are Y (Triangle) and B (Circle),
// and the acquire/enter/jump hints' A is Cross
const Text kPlaystationHints[] = {
    {"TBS_INSTRUCTION_BREAK_FREE", {"[Triangle] ", "[Triangle] ", "[Triángulo] ", "[Triangolo] "}},
    {"TBS_INSTRUCTION_HAMMERTOSS", {"[Triangle] ", "[Triangle] ", "[Triángulo] ", "[Triangolo] "}},
    {"TBS_INSTRUCTION_WEAPONIZE", {"[Triangle] ", "[Triangle] ", "[Triángulo] ", "[Triangolo] "}},
    {"TBS_INSTRUCTION_DROP", {"[Circle] ", "[Rond] ", "[Círculo] ", "[Cerchio] "}},
};

const char* HintLabel(const std::u16string& key, int column, bool playstation) {
    const Text* const table = playstation ? kPlaystationHints : kHintKeys;
    const size_t count = playstation ? std::size(kPlaystationHints) : std::size(kHintKeys);
    for (size_t i = 0; i < count; i++)
        if (KeyIs(key, table[i].key)) return table[i].text[column];
    static const char* const kSpace[4] = {"[Space] ", "[Espace] ", "[Espacio] ", "[Spazio] "};
    static const char* const kCross[4] = {"[Cross] ", "[Croix] ", "[Cruz] ", "[Croce] "};
    return playstation ? kCross[column] : kSpace[column];
}

// a string table's lines with each button prompt as its key, its press start lines in the keyboard's or the
// PlayStation's words
std::vector<uint8_t> KeyboardPrompts(const std::vector<uint8_t>& in, int column, bool playstation) {
    const Text* const lines = playstation ? kPlaystationLines : kKeyboardLines;
    const size_t line_count = playstation ? std::size(kPlaystationLines) : std::size(kKeyboardLines);
    const Prompt* const prompts = playstation ? kPlaystationPrompts : kKeyboardPrompts;
    std::vector<uint8_t> out(in.begin(), in.begin() + 2);
    std::u16string text(reinterpret_cast<const char16_t*>(in.data() + 2), (in.size() - 2) / 2);
    std::u16string line_key;
    size_t line_end = 0;  // where the rewritten line ends, when one was
    for (size_t i = 0; i < text.size(); i++) {
        if (i == 0 || text[i - 1] == u'\n') {
            size_t tab = text.find(u'\t', i);
            const size_t newline = text.find(u'\n', i);
            if (tab == std::u16string::npos || (newline != std::u16string::npos && tab > newline)) tab = std::u16string::npos;
            line_key = text.substr(i, tab == std::u16string::npos ? 0 : tab - i);
            line_end = 0;
            if (tab != std::u16string::npos) {
                size_t end = text.find(u'\n', i);
                end = end == std::u16string::npos ? text.size() : end + 1;
                for (size_t k = 0; k < line_count; k++) {
                    if (!KeyIs(line_key, lines[k].key)) continue;
                    AppendUtf16(out, lines[k].key);
                    AppendUtf16(out, "\t");
                    AppendUtf16(out, lines[k].text[column]);
                    AppendUtf16(out, "\r\n");
                    line_end = end;
                    break;
                }
                if (!line_end && StartsWith(line_key, "TBS_INSTRUCTION_")) {
                    // the HUD's hint text: the key that does it, then the instruction as the game wrote it
                    const size_t from = tab + 1;
                    std::u16string rest = text.substr(from, end - from);
                    while (!rest.empty() && (rest.back() == u'\r' || rest.back() == u'\n')) rest.pop_back();
                    AppendUtf16(out, std::string(line_key.begin(), line_key.end()).c_str());
                    AppendUtf16(out, "\t");
                    AppendUtf16(out, HintLabel(line_key, column, playstation));
                    for (char16_t c : rest) {
                        out.push_back(uint8_t(c));
                        out.push_back(uint8_t(c >> 8));
                    }
                    AppendUtf16(out, "\r\n");
                    line_end = end;
                }
                if (line_end) i = line_end - 1;
            }
            if (line_end) continue;
        }
        if (text[i] == u'\\' && i + 3 < text.size() && text[i + 1] == u'b') {
            const Prompt* p = nullptr;
            for (size_t k = 0; k < (playstation ? std::size(kPlaystationPrompts) : std::size(kKeyboardPrompts)); k++)
                if (text[i + 2] == char16_t(prompts[k].code[0]) && text[i + 3] == char16_t(prompts[k].code[1])) p = &prompts[k];
            if (p) {
                AppendUtf16(out, p->text[column]);
                i += 3;
                continue;
            }
        }
        out.push_back(uint8_t(text[i]));
        out.push_back(uint8_t(text[i] >> 8));
    }
    return out;
}

// a string table (UTF-16 lines of key, tab, text) with the menu's lines after its own, as the controller's prompts,
// as the keyboard's and as the PlayStation's, the same length: the shorter ends in a comment line of spaces
struct EditedStrings {
    std::vector<uint8_t> controller, keyboard, playstation;
};
std::optional<EditedStrings> EditStrings(const std::vector<uint8_t>& in, int column, const char* name) {
    if (in.size() < 4 || in[0] != 0xFF || in[1] != 0xFE) {
        LOGW("display menu: %s is not a UTF-16 string table", name);
        return std::nullopt;
    }
    std::vector<uint8_t> pad = in;
    bool ends_with_newline = pad[pad.size() - 2] == '\n' && pad[pad.size() - 1] == 0;
    if (!ends_with_newline) AppendUtf16(pad, "\r\n");
    for (const Text& t : kTexts) {
        AppendUtf16(pad, t.key);
        AppendUtf16(pad, "\t");
        AppendUtf16(pad, t.text[column]);
        AppendUtf16(pad, "\r\n");
    }
    EditedStrings out;
    out.controller = pad;
    out.keyboard = KeyboardPrompts(pad, column, false);
    out.playstation = KeyboardPrompts(pad, column, true);
    const size_t length = std::max({pad.size(), out.keyboard.size(), out.playstation.size()}) + 6;  // room for the shortest comment line, "#\r\n"
    for (std::vector<uint8_t>* v : {&out.controller, &out.keyboard, &out.playstation}) {
        AppendUtf16(*v, "#");
        while (v->size() < length - 4) AppendUtf16(*v, " ");
        AppendUtf16(*v, "\r\n");
    }
    return out;
}

bool EqualNoCase(const std::string& a, const char* b) {
    size_t n = std::strlen(b);
    if (a.size() != n) return false;
    for (size_t i = 0; i < n; i++)
        if (std::tolower(uint8_t(a[i])) != std::tolower(uint8_t(b[i]))) return false;
    return true;
}

// misc.rcf as the game reads it: the edited files past its end, the directory pointing at them
struct RcfView {
    uint64_t dir_at = 0;
    std::vector<uint8_t> dir;  // the directory, edited
    struct Added {
        uint64_t at;
        std::vector<uint8_t> bytes;
        bool script;                    // the selections follow the settings, the rest stays
        std::vector<size_t> selection;  // a script's named values' digits (Script::selection)
        std::vector<uint8_t> keyboard;    // a string table's keyboard prompts, as long as bytes
        std::vector<uint8_t> playstation;  // its PlayStation prompts, as long as bytes
        int column = -1;                // a string table's language
    };
    std::vector<Added> added;

    void Read(uint64_t at, uint8_t* dst, size_t n) const {
        auto overlay = [&](uint64_t from, const uint8_t* src, size_t len) {
            uint64_t lo = std::max(at, from), hi = std::min<uint64_t>(at + n, from + len);
            if (lo < hi) std::memcpy(dst + (lo - at), src + (lo - from), size_t(hi - lo));
        };
        overlay(dir_at, dir.data(), dir.size());
        for (const Added& a : added) {
            if (!a.script) {
                const std::vector<uint8_t>* text = &a.bytes;
                if (!a.keyboard.empty()) {
                    switch (GetPromptDevice()) {
                        case PromptDevice::Keyboard: text = &a.keyboard; break;
                        case PromptDevice::Playstation: text = &a.playstation; break;
                        default: text = g_controller_prompts.load() ? &a.bytes : &a.keyboard; break;
                    }
                }
                const auto& b = *text;
                if (a.column >= 0 && at < a.at + b.size() && a.at < at + n) g_language.store(a.column);
                overlay(a.at, b.data(), b.size());
                continue;
            }
            overlay(a.at, a.bytes.data(), a.bytes.size());
            for (size_t i = 0; i < a.selection.size(); i++) {
                const char digit = Digit(int(i));
                overlay(a.at + a.selection[i], reinterpret_cast<const uint8_t*>(&digit), 1);
            }
        }
    }
};

std::shared_ptr<RcfView> MakeRcfView(uint64_t size, const std::function<size_t(uint64_t, void*, size_t)>& read) {
    auto bytes = [&](uint64_t at, size_t n) {
        std::vector<uint8_t> b(n);
        b.resize(read(at, b.data(), n));
        return b;
    };
    std::optional<Rcf> rcf = ReadRcf(size, read);  // rcf.h
    if (!rcf) return nullptr;
    const uint32_t dir_at = rcf->dir_at;
    std::vector<uint8_t>& dir = rcf->dir;
    auto find = [&](const char* name) { return rcf->Find(name); };
    auto contents = [&](uint32_t e) { return bytes(rcf->Offset(e), rcf->Size(e)); };

    auto view = std::make_shared<RcfView>();
    view->dir_at = dir_at;
    uint64_t end = (size + kRcfAlign - 1) & ~uint64_t(kRcfAlign - 1);
    auto add = [&](uint32_t e, std::vector<uint8_t> b, const Script* script, std::vector<uint8_t> keyboard = {},
                   std::vector<uint8_t> playstation = {}, int column = -1) {
        uint32_t at = uint32_t(end), n = uint32_t(b.size());
        std::memcpy(dir.data() + e * 12 + 4, &at, 4);
        std::memcpy(dir.data() + e * 12 + 8, &n, 4);
        RcfView::Added a{end, std::move(b), script != nullptr, {}, std::move(keyboard), std::move(playstation), column};
        if (script) a.selection = script->selection;
        view->added.push_back(std::move(a));
        end = (end + n + kRcfAlign - 1) & ~uint64_t(kRcfAlign - 1);
    };

    std::vector<std::pair<uint32_t, Script>> scripts;  // directory entry, edited
    for (const Layout& l : kLayouts) {
        auto e = find(l.file);
        if (!e) continue;
        std::vector<uint8_t> raw = contents(*e);
        std::string text(raw.begin(), raw.end());
        SwapInvertLabels(text);
        if (auto script = EditScript(text, l)) scripts.emplace_back(*e, std::move(*script));
    }
    if (scripts.empty()) return nullptr;
    for (auto& [name, column] : kLanguages) {
        auto e = find(name);
        if (!e) continue;  // a release without that language
        auto edited = EditStrings(contents(*e), column, name);
        if (!edited) return nullptr;
        add(*e, std::move(edited->controller), nullptr, std::move(edited->keyboard), std::move(edited->playstation), column);
    }
    for (auto& [e, script] : scripts) add(e, std::vector<uint8_t>(script.text.begin(), script.text.end()), &script);
    view->dir = std::move(dir);
    return view;
}

}  // namespace

std::shared_ptr<app::FileView> GameFileView(const std::string& path, uint64_t size,
                                            const std::function<size_t(uint64_t, void*, size_t)>& read) {
    const std::string suffix = "\\misc.rcf";
    if (path.size() < suffix.size() || !EqualNoCase(path.substr(path.size() - suffix.size()), suffix.c_str()))
        return nullptr;
    {
        std::lock_guard lock(g_lock);
        if (!g_ready.load()) {
            g_options = MakeOptions(app::Get());
            g_ready.store(true);
        }
    }
    std::shared_ptr<RcfView> rcf = MakeRcfView(size, read);
    if (!rcf) {
        LOGW("display menu: %s could not be read as a cement library, Options stays as it is", path.c_str());
        return nullptr;
    }
    g_served.store(true);
    auto view = std::make_shared<app::FileView>();
    const auto& last = rcf->added.back();
    view->size = last.at + last.bytes.size();
    view->read = [rcf](uint64_t at, uint8_t* dst, size_t n) { rcf->Read(at, dst, n); };
    LOGI("display menu: %s serves Options > Display Options and Cheats (%zu files edited)", path.c_str(), rcf->added.size());
    return view;
}

void SetControllerPrompts(bool controller) { g_controller_prompts.store(controller); }

bool ControllerPrompts() { return g_controller_prompts.load(); }

PromptDevice GetPromptDevice() { return PromptDevice(g_prompt_device.load()); }

void SetPromptDevice(PromptDevice device) {
    g_prompt_device.store(int(device));
    if (device != PromptDevice::Auto) SetControllerPrompts(device == PromptDevice::Controller);
}

const char* PromptDeviceName(PromptDevice device) {
    switch (device) {
        case PromptDevice::Keyboard: return "keyboard";
        case PromptDevice::Controller: return "controller";
        case PromptDevice::Playstation: return "playstation";
        default: return "auto";
    }
}

// an empty string in the guest's memory, for the HUD hints whose button icon the keyboard's prompts leave out

void MenuButtonPrompts(uint32_t owner) {
    const bool controller = g_controller_prompts.load();
    const bool playstation = GetPromptDevice() == PromptDevice::Playstation;
    const int column = g_language.load();
    for (uint32_t i = 0; i < std::size(kMenuPrompts); i++) {
        std::vector<uint8_t> text;
        if (controller) AppendUtf16(text, (std::string("") + kMenuPrompts[i].code).c_str());
        else if (playstation) AppendUtf16(text, kPlaystationMenuPrompts[i].text[column]);
        else AppendUtf16(text, kMenuPrompts[i].text[column]);
        text.push_back(0), text.push_back(0);
        if (void* icon = kernel::HostPtr(owner + kMenuIcons + i * kMenuIconSize); icon && text.size() <= kMenuIconSize)
            std::memcpy(icon, text.data(), text.size());
    }
}

void RegisterDisplayCallbacks(X86Context& ctx) {
    if (!g_served.load()) return;  // the game reads its own script, which names none of them
    // each selector's int callback and each screen's sync: name, owner, sync
    std::vector<std::tuple<std::string, uint32_t, bool>> callbacks;
    for (int s = 0; s < kSettings; s++) callbacks.emplace_back(std::string(g_options[s].named_value) + "Changed", kOwnerBase + s, false);
    callbacks.emplace_back(kSyncName, kOwnerBase + kSettings, true);
    for (int i = 0; i < kCheatRowCount; i++)
        callbacks.emplace_back(std::string("PCCheat") + kCheatRows[i].name + "Changed", kCheatOwnerBase + i, false);
    callbacks.emplace_back(kCheatSyncName, kCheatSyncOwner, true);
    const uint32_t esp = ctx.esp;
    for (const auto& [name, owner, sync] : callbacks) {
        // the name only lives through the call, below the guest's stack
        ctx.esp = (esp - 64) & ~3u;
        std::memcpy(kernel::HostPtr(ctx.esp), name.c_str(), name.size() + 1);
        uint32_t functor = kernel::CallGuest(ctx, kGuestNew, {12});
        if (!functor) break;
        kernel::W32(functor, sync ? kFunctorVtable : kIntFunctorVtable);
        kernel::W32(functor + 4, owner);
        kernel::W32(functor + 8, sync ? kSyncHandler : kSharedHandler);
        kernel::CallGuest(ctx, sync ? kRegisterCallback : kRegisterIntCallback, {ctx.esp, functor});
    }
    ctx.esp = esp;
}

namespace {
// a selector of the current screen by name, set to a choice (the European camera options at 0x000B5C50 do so)
void SetSelector(X86Context& ctx, const char* name, uint32_t choice) {
    const uint32_t esp = ctx.esp, ecx = ctx.ecx;
    ctx.esp = (esp - 64) & ~3u;
    std::memcpy(kernel::HostPtr(ctx.esp), name, std::strlen(name) + 1);
    uint32_t widget = kernel::CallGuest(ctx, kFindWidget, {ctx.esp});
    uint32_t selector = widget ? kernel::CallGuest(ctx, kDynamicCast, {widget, 0, kWidgetType, kTextSelectorType, 0}) : 0;
    if (selector) {
        ctx.ecx = selector;
        kernel::CallGuest(ctx, kernel::R32(kernel::R32(selector) + kSetSelection), {choice});
    }
    ctx.esp = esp;
    ctx.ecx = ecx;
}
}  // namespace

bool DisplaySync(X86Context& ctx, uint32_t owner) {
    if (owner == kCheatSyncOwner) {
        // the cheats' selectors show their levels, which the other menu (main or pause) may have changed
        for (const CheatRow& r : kCheatRows) SetSelector(ctx, CheatSelector(r).c_str(), uint32_t(cheats::Level(r.cheat)));
        return true;
    }
    if (owner != kOwnerBase + kSettings) return false;
    if (!g_ready.load()) return true;
    for (int s = 0; s < kSettings; s++) SetSelector(ctx, g_options[s].selector, uint32_t(Selection(Setting(s)) - '0'));
    return true;
}

bool DisplayCallback(uint32_t owner, uint32_t value) {
    if (owner >= kCheatOwnerBase && owner < kCheatOwnerBase + uint32_t(kCheatRowCount)) {
        const CheatRow& r = kCheatRows[owner - kCheatOwnerBase];
        if (value < r.choices.size()) cheats::Select(r.cheat, int(value));  // cheats.h saves it
        return true;
    }
    if (owner < kOwnerBase || owner >= kOwnerBase + kSettings) return false;
    Setting s = Setting(owner - kOwnerBase);
    if (!g_ready.load() || value >= g_options[s].entries.size()) return true;
    const Entry& e = g_options[s].entries[value];
    app::Settings& st = app::Get();
    if (Matches(s, e, st)) return true;
    Assign(s, e, st);
    LOGI("display menu: %s -> %s", g_options[s].named_value, e.key);
    if (!InGame(s)) g_display_changed.store(true);
    g_changed_at.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
    return true;
}

void NoteSettingChanged() {
    g_changed_at.store(std::chrono::steady_clock::now().time_since_epoch().count(), std::memory_order_relaxed);
}

void TickDisplaySettings() {
    int64_t at = g_changed_at.load(std::memory_order_relaxed);
    if (!at) return;
    auto since = std::chrono::steady_clock::now() - std::chrono::steady_clock::time_point(std::chrono::steady_clock::duration(at));
    if (since < kSettleTime || !g_changed_at.compare_exchange_strong(at, 0, std::memory_order_relaxed)) return;
    if (g_display_changed.exchange(false)) app::ApplyDisplay();  // the view's settings apply as they change
    app::Save();
}

}  // namespace hulk

