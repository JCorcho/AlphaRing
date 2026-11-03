#include "MenuSettings.h"

#include "log/Log.h"
#include "log/MatchLog.h"
#include "global/Global.h"
#include "filesystem/Filesystem.h"
#include "mcc/CGameManager.h"
#include "mcc/CGameGlobal.h"
#include "mcc/mcc.h"
#include "render/imgui/game/xbox/CXboxColorMapping.h"
#include "input/PlayStation.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>

namespace AlphaRing::MenuSettings {
    static const std::string& FilePath() {
        static std::string path = AlphaRing::Filesystem::ModuleFile("alpha_ring_menu.bin");
        return path;
    }

    // "ARMS" - files from before the menus were united start with playerCount
    // (1-4) instead and hold the menu state alone.
    static constexpr uint32_t k_magic = 0x534D5241;
    // 3 added usePlayer0Profile to the menu state block, 4 the per-player armor,
    // 5 the characters' player names, 6 the players' names, 7 the profiles'
    // built-in index, 8 the per-game armor and the characters' game, 9 a
    // key/value block at the end (the "MCC" setting values, the new per-player
    // options, the copied MCC colors), which later additions extend without a
    // new version
    static constexpr uint32_t k_version = 9;

    // Caps on counts read from the file, so a corrupt one can't ask for a huge allocation.
    static constexpr uint32_t k_maxEntries = 1024;
    static constexpr uint32_t k_maxString = 1024;

    static const char* const k_defaultProfileNames[MenuConfig::k_profileCount] = {
        "Default", "Southpaw", "Boxer", "Green Thumb", "Bumper Jumper", "Recon",
    };

    // In file order; append only, so older files keep reading.
    static const struct { const char* name; CGamepadMapping::eButton ControllerProfileMapping::* field; } k_profileActions[k_profileActionCount] = {
        {"Jump",                     &ControllerProfileMapping::jump},
        {"Switch Grenades",          &ControllerProfileMapping::switchGrenades},
        {"Use Equipment",            &ControllerProfileMapping::useEquipment},
        {"Action / Interact",        &ControllerProfileMapping::actionInteract},
        {"Reload Right Weapon",      &ControllerProfileMapping::reloadRightWeapon},
        {"Swap/Reload Left Weapon",  &ControllerProfileMapping::swapReloadLeftWeapon},
        {"Change Weapon",            &ControllerProfileMapping::changeWeapon},
        {"Melee",                    &ControllerProfileMapping::melee},
        {"Toggle Flashlight",        &ControllerProfileMapping::toggleFlashlight},
        {"Throw Grenade",            &ControllerProfileMapping::throwGrenade},
        {"Shoot",                    &ControllerProfileMapping::shoot},
        {"Crouch",                   &ControllerProfileMapping::crouch},
        {"Player Zoom",              &ControllerProfileMapping::playerZoom},
        {"Multiplayer Scoreboard",   &ControllerProfileMapping::multiplayerScoreboard},
    };

    // The CGamepadMapping actions a controller profile sets, mirrors included.
    static const struct { int action; CGamepadMapping::eButton ControllerProfileMapping::* field; } k_profileBindings[] = {
        {0,  &ControllerProfileMapping::jump},
        {1,  &ControllerProfileMapping::switchGrenades},
        {2,  &ControllerProfileMapping::actionInteract},
        {3,  &ControllerProfileMapping::reloadRightWeapon},
        {4,  &ControllerProfileMapping::changeWeapon},
        {13, &ControllerProfileMapping::swapReloadLeftWeapon},
        {5,  &ControllerProfileMapping::melee},
        {6,  &ControllerProfileMapping::toggleFlashlight},
        {7,  &ControllerProfileMapping::throwGrenade},
        {49, &ControllerProfileMapping::throwGrenade},  // Use Left Weapon mirrors Throw Grenade
        {28, &ControllerProfileMapping::throwGrenade},  // Thrust mirrors Throw Grenade
        {24, &ControllerProfileMapping::throwGrenade},  // Vehicle Function 1 mirrors Throw Grenade
        {8,  &ControllerProfileMapping::shoot},
        {9,  &ControllerProfileMapping::crouch},
        {21, &ControllerProfileMapping::crouch},        // Vehicle Function 2 mirrors Crouch
        {10, &ControllerProfileMapping::playerZoom},
        {20, &ControllerProfileMapping::multiplayerScoreboard},
        {23, &ControllerProfileMapping::useEquipment},
        {22, &ControllerProfileMapping::jump},          // Vehicle Function 3 mirrors Jump
    };

    // In file order; append only. Colors are left out: the menu state's
    // per-player colors own them and are reapplied at every game start.
    static const struct { const char* name; int CUserProfile::* field; } k_characterFields[] = {
        {"PlayerModelPermutation",  &CUserProfile::PlayerModelPermutation},
        {"HelmetIndex",             &CUserProfile::HelmetIndex},
        {"LeftShoulderIndex",       &CUserProfile::LeftShoulderIndex},
        {"RightShoulderIndex",      &CUserProfile::RightShoulderIndex},
        {"ChestIndex",              &CUserProfile::ChestIndex},
        {"WristIndex",              &CUserProfile::WristIndex},
        {"UtilityIndex",            &CUserProfile::UtilityIndex},
        {"ArmsIndex",               &CUserProfile::ArmsIndex},
        {"LegsIndex",               &CUserProfile::LegsIndex},
        {"BackpackIndex",           &CUserProfile::BackpackIndex},
        {"SpartanBodyIndex",        &CUserProfile::SpartanBodyIndex},
        {"SpartanArmorEffectIndex", &CUserProfile::SpartanArmorEffectIndex},
        {"KneesIndex",              &CUserProfile::KneesIndex},
        {"VisorColorIndex",         &CUserProfile::VisorColorIndex},
        {"EliteHelmetIndex",        &CUserProfile::EliteHelmetIndex},
        {"EliteLeftShoulderIndex",  &CUserProfile::EliteLeftShoulderIndex},
        {"EliteRightShoulderIndex", &CUserProfile::EliteRightShoulderIndex},
        {"EliteChestIndex",         &CUserProfile::EliteChestIndex},
        {"EliteArmsIndex",          &CUserProfile::EliteArmsIndex},
        {"EliteLegsIndex",          &CUserProfile::EliteLegsIndex},
        {"EliteArmorIndex",         &CUserProfile::EliteArmorIndex},
        {"EliteArmorEffectIndex",   &CUserProfile::EliteArmorEffectIndex},
        {"VoiceIndex",              &CUserProfile::VoiceIndex},
        {"SpartanPose",             &CUserProfile::SpartanPose},
        {"ElitePose",               &CUserProfile::ElitePose},
    };
    // Value 0 of a record is UseEliteModel, the fields above follow it.
    static constexpr int k_characterFieldCount = 1 + (int)std::size(k_characterFields);

    // ---------------------------------------------------------------- armor layout

    // Character::values / Armor() layout: [0] UseEliteModel (no longer set),
    // then k_characterFields, then the four ServiceTag characters.
    static constexpr int k_serviceTagValue = k_characterFieldCount;
    static constexpr int k_serviceTagLength = (int)std::size(CUserProfile{}.ServiceTag);
    static constexpr int k_armorValueCount = k_serviceTagValue + k_serviceTagLength;

    static const char* const k_armorGameNames[ArmorGameCount] = {"Halo 2A", "Halo 3", "Halo Reach", "Halo 4"};
    static const int k_armorItemGames[ArmorGameCount] = {
        CUserProfile::ItemHalo2A, CUserProfile::ItemHalo3, CUserProfile::ItemReach, CUserProfile::ItemHalo4,
    };

    // The ArmorGame an item belongs to, or -1.
    static int ArmorGameOfItem(int item) {
        int game = CUserProfile::CustomizationItemGame(item);
        for (int g = 0; g < ArmorGameCount; ++g)
            if (k_armorItemGames[g] == game) return g;
        return -1;
    }

    // An item field holds an item; the others (UseEliteModel,
    // PlayerModelPermutation) are plain numbers.
    static bool IsItemValue(int v) {
        return v > 0 && v < k_characterFieldCount && k_characterFields[v - 1].field != &CUserProfile::PlayerModelPermutation;
    }

    static std::vector<int> EmptyArmor() {
        std::vector<int> values(k_armorValueCount, -1);
        for (int i = 0; i < k_serviceTagLength; ++i) values[k_serviceTagValue + i] = 0;
        return values;
    }

    static bool HasValues(const std::vector<int>& values) {
        for (int v = 0; v < (int)values.size() && v < k_serviceTagValue; ++v)
            if (values[v] >= 0) return true;
        return (int)values.size() > k_serviceTagValue && values[k_serviceTagValue] != 0;
    }

    // A set from before armor was kept per game, split by the game each item
    // is from. The ServiceTag goes to every game's; the plain numbers to none.
    static std::array<std::vector<int>, ArmorGameCount> SplitByGame(const std::vector<int>& values) {
        std::array<std::vector<int>, ArmorGameCount> split;
        for (auto& armor : split) armor = EmptyArmor();
        for (int v = 0; v < (int)values.size() && v < k_serviceTagValue; ++v) {
            if (!IsItemValue(v) || values[v] < 0) continue;
            int game = ArmorGameOfItem(values[v]);
            if (game >= 0) split[game][v] = values[v];
        }
        if ((int)values.size() >= k_armorValueCount && values[k_serviceTagValue] != 0)
            for (auto& armor : split)
                for (int i = 0; i < k_serviceTagLength; ++i)
                    armor[k_serviceTagValue + i] = values[k_serviceTagValue + i];
        return split;
    }

    static std::recursive_mutex s_mutex;
    static bool s_loaded = false;
    // The file is there but couldn't be opened: saving would replace the
    // player's settings with the defaults loaded instead, so saves are refused.
    static bool s_unreadable = false;
    static MenuState s_state;
    static std::vector<ControllerProfile> s_profiles;
    static std::vector<Character> s_characters;
    static std::vector<int> s_armor[4][ArmorGameCount]; // in Character::values layout; empty = none
    static std::string s_playerNames[4]; // UTF-8; empty = the default "Player N"
    static constexpr int k_gameCount = 7;   // CGameGlobal::eGame
    static constexpr int k_lookColorCount = 6; // PlayerModel{Primary,Secondary,Tertiary}ColorIndex, then ...Color
    static std::vector<int> s_lookColors[4][k_gameCount]; // empty = none

    static MenuState DefaultState() {
        MenuState ms{};
        ms.playerCount = 1;
        ms.useKM = false;
        ms.usePlayer0Profile = false;
        ms.anyoneCanPause = true;
        ms.nativePlayStation = true;
        ms.halo2CompositeClip = false;
        ms.classicGraphics3P = false;
        for (int i = 0; i < 4; ++i) {
            ms.controllerIndex[i] = i;
            // player 1 keeps the buttons set in MCC; the others start on Default
            ms.controllerProfile[i] = i == 0 ? -1 : 0;
            ms.sensitivity[i] = 0;
            ms.invert[i] = -1;
            ms.invertFlight[i] = -1;
            ms.vibration[i] = -1;
            ms.deadzone[i] = 0;
            ms.teamIndex[i] = i % 2;
            for (int& color : ms.playerColors[i].colors) color = -1;
        }
        return ms;
    }

    static void SeedProfiles() {
        s_profiles.clear();
        for (int i = 0; i < MenuConfig::k_profileCount; ++i)
            s_profiles.push_back({k_defaultProfileNames[i], g_menuConfig.controllerProfiles[i], i});
    }

    // ---------------------------------------------------------------- file

    template <typename T> static void Write(std::ostream& os, const T& v) {
        os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    }

    template <typename T> static bool Read(std::istream& is, T& v) {
        return (bool)is.read(reinterpret_cast<char*>(&v), sizeof(T));
    }

    static void WriteString(std::ostream& os, const std::string& s) {
        Write(os, (uint32_t)s.size());
        os.write(s.data(), (std::streamsize)s.size());
    }

    static bool ReadString(std::istream& is, std::string& s) {
        uint32_t len;
        if (!Read(is, len) || len > k_maxString) return false;
        s.resize(len);
        return len == 0 || (bool)is.read(s.data(), len);
    }

    static bool ReadValues(std::istream& is, std::vector<int>& out) {
        uint32_t count;
        if (!Read(is, count) || count > k_maxEntries) return false;
        out.resize(count);
        for (uint32_t v = 0; v < count; ++v) {
            int32_t value;
            if (!Read(is, value)) return false;
            out[v] = value;
        }
        return true;
    }

    // The layout the Xbox menu has always written, kept as the file's first block.
    static void WriteMenuState(std::ostream& os, const MenuState& ms) {
        Write(os, ms.playerCount);
        Write(os, (uint8_t)(ms.useKM ? 1 : 0));
        Write(os, ms.controllerIndex);
        Write(os, ms.controllerProfile);
        Write(os, ms.sensitivity);
        for (int invert : ms.invert) Write(os, (uint8_t)(invert == 1 ? 1 : 0)); // -1 is in the key/value block
        Write(os, ms.teamIndex);
        Write(os, ms.playerColors);
        Write(os, (uint8_t)(ms.usePlayer0Profile ? 1 : 0));
    }

    // `version` 0 is a file from before the menus were united.
    static bool ReadMenuState(std::istream& is, MenuState& ms, uint32_t version) {
        uint8_t useKM = 0;
        uint8_t invert[4] = {};
        bool ok = Read(is, ms.playerCount) && Read(is, useKM) && Read(is, ms.controllerIndex) &&
                  Read(is, ms.controllerProfile) && Read(is, ms.sensitivity) && Read(is, invert) &&
                  Read(is, ms.teamIndex) && Read(is, ms.playerColors);
        uint8_t usePlayer0Profile = 0;
        if (ok && version >= 3)
            ok = Read(is, usePlayer0Profile);
        ms.usePlayer0Profile = usePlayer0Profile != 0;
        // Unchecked file data: the count indexes the four player profiles and
        // per-player settings, so keep it to 1-4 even for a corrupt file.
        ms.playerCount = std::clamp(ms.playerCount, 1, 4);
        ms.useKM = useKM != 0;
        for (int i = 0; i < 4; ++i) {
            ms.invert[i] = invert[i] != 0 ? 1 : 0;
            ms.controllerIndex[i] = std::clamp(ms.controllerIndex[i], 0, 4); // 4 = no controller
            ms.sensitivity[i] = std::clamp(ms.sensitivity[i], 0, 20);
            ms.teamIndex[i] = ms.teamIndex[i] ? 1 : 0;
            for (int& color : ms.playerColors[i].colors)
                color = std::clamp(color, -1, CXboxColorMapping::kColorCount - 1);
        }
        return ok;
    }

    // ---------------------------------------------------------------- key/value block (version 9)

    using KeyValues = std::map<std::string, int32_t>;

    static bool ReadKeyValues(std::istream& is, KeyValues& kv) {
        uint32_t count;
        if (!Read(is, count) || count > k_maxEntries * 16) return false;
        for (uint32_t i = 0; i < count; ++i) {
            std::string key;
            int32_t value;
            if (!ReadString(is, key) || !Read(is, value)) return false;
            kv[key] = value;
        }
        return true;
    }

    static void WriteKeyValues(std::ostream& os, const KeyValues& kv) {
        Write(os, (uint32_t)kv.size());
        for (auto& [key, value] : kv) {
            WriteString(os, key);
            Write(os, value);
        }
    }

    static std::string PlayerKey(int player, const char* name) {
        return "p" + std::to_string(player) + "." + name;
    }

    static std::string LookColorKey(int player, int game, int k) {
        return "p" + std::to_string(player) + ".g" + std::to_string(game) + ".color" + std::to_string(k);
    }

    static std::string CharacterColorKey(int character, int k) {
        return "c" + std::to_string(character) + ".color" + std::to_string(k);
    }

    static KeyValues CollectKeyValues() {
        KeyValues kv;
        auto& ms = s_state;
        for (int i = 0; i < 4; ++i) {
            kv[PlayerKey(i, "invert")] = ms.invert[i];
            kv[PlayerKey(i, "invertFlight")] = ms.invertFlight[i];
            kv[PlayerKey(i, "vibration")] = ms.vibration[i];
            kv[PlayerKey(i, "deadzone")] = ms.deadzone[i];
            kv[PlayerKey(i, "controllerProfile")] = ms.controllerProfile[i]; // -1 doesn't fit the old block's readers
            for (int g = 0; g < k_gameCount; ++g)
                for (int k = 0; k < (int)s_lookColors[i][g].size() && k < k_lookColorCount; ++k)
                    if (s_lookColors[i][g][k] >= 0)
                        kv[LookColorKey(i, g, k)] = s_lookColors[i][g][k];
        }
        kv["anyoneCanPause"] = ms.anyoneCanPause ? 1 : 0;
        kv["nativePlayStation"] = ms.nativePlayStation ? 1 : 0;
        kv["halo2CompositeClip"] = ms.halo2CompositeClip ? 1 : 0;
        kv["classicGraphics3P"] = ms.classicGraphics3P ? 1 : 0;
        for (int c = 0; c < (int)s_characters.size(); ++c)
            for (int k = 0; k < (int)s_characters[c].colors.size() && k < k_lookColorCount; ++k)
                if (s_characters[c].colors[k] >= 0)
                    kv[CharacterColorKey(c, k)] = s_characters[c].colors[k];
        return kv;
    }

    static void ApplyKeyValues(const KeyValues& kv) {
        auto get = [&](const std::string& key, int fallback) {
            auto it = kv.find(key);
            return it == kv.end() ? fallback : (int)it->second;
        };
        auto& ms = s_state;
        for (int i = 0; i < 4; ++i) {
            ms.invert[i] = std::clamp(get(PlayerKey(i, "invert"), ms.invert[i]), -1, 1);
            ms.invertFlight[i] = std::clamp(get(PlayerKey(i, "invertFlight"), ms.invertFlight[i]), -1, 1);
            ms.vibration[i] = std::clamp(get(PlayerKey(i, "vibration"), ms.vibration[i]), -1, 1);
            ms.deadzone[i] = std::clamp(get(PlayerKey(i, "deadzone"), ms.deadzone[i]), 0, k_maxDeadzone);
            ms.controllerProfile[i] = get(PlayerKey(i, "controllerProfile"), ms.controllerProfile[i]);
            for (int g = 0; g < k_gameCount; ++g) {
                std::vector<int> colors(k_lookColorCount, -1);
                bool any = false;
                for (int k = 0; k < k_lookColorCount; ++k) {
                    colors[k] = get(LookColorKey(i, g, k), -1);
                    any |= colors[k] >= 0;
                }
                if (any) s_lookColors[i][g] = std::move(colors);
            }
        }
        ms.anyoneCanPause = get("anyoneCanPause", ms.anyoneCanPause) != 0;
        ms.nativePlayStation = get("nativePlayStation", ms.nativePlayStation) != 0;
        ms.halo2CompositeClip = get("halo2CompositeClip", ms.halo2CompositeClip) != 0;
        ms.classicGraphics3P = get("classicGraphics3P", ms.classicGraphics3P) != 0;
        for (int c = 0; c < (int)s_characters.size(); ++c)
            for (int k = 0; k < k_lookColorCount; ++k)
                s_characters[c].colors[k] = get(CharacterColorKey(c, k), -1);
    }

    // A file from before version 9 has no "MCC" values: the settings it
    // holds at their old defaults (look not inverted, sensitivity 3, Steel
    // colors, player 1 on the Default profile) were never picked, and stopped
    // the MCC profile's own from reaching the game. They become "MCC".
    static void MigrateToMccDefaults(MenuState& ms) {
        for (int i = 0; i < 4; ++i) {
            if (ms.invert[i] == 0) ms.invert[i] = -1;
            if (ms.sensitivity[i] == 3) ms.sensitivity[i] = 0;
            auto& colors = ms.playerColors[i].colors;
            if (colors[0] == 0 && colors[1] == 0 && colors[2] == 0)
                colors[0] = colors[1] = colors[2] = -1;
        }
        if (ms.controllerProfile[0] == 0) ms.controllerProfile[0] = -1;
    }

    static void Load() {
        s_state = DefaultState();
        s_profiles.clear();
        s_characters.clear();
        for (auto& player : s_armor)
            for (auto& armor : player) armor.clear();
        for (auto& player : s_lookColors)
            for (auto& colors : player) colors.clear();
        for (auto& name : s_playerNames) name.clear();
        bool stateRead = false;
        KeyValues kv;
        bool kvRead = false;

        std::ifstream ifs(FilePath(), std::ios::binary);
        if (!ifs && GetFileAttributesA(FilePath().c_str()) != INVALID_FILE_ATTRIBUTES) {
            s_unreadable = true;
            LOG_ERROR("MenuSettings: can't open {}, so menu settings won't be saved this session", FilePath());
        } else if (ifs) {
            LOG_INFO("MenuSettings: loading {}", FilePath());
        }
        uint32_t magic = 0, version = 0;
        if (ifs && Read(ifs, magic)) {
            if (magic != k_magic) {
                ifs.seekg(0);
                stateRead = ReadMenuState(ifs, s_state, 0);
            } else if (Read(ifs, version) && ReadMenuState(ifs, s_state, version)) {
                stateRead = true;
                uint32_t count, buttons;
                bool profilesRead = false;
                if (Read(ifs, count) && count <= k_maxEntries) {
                    uint32_t i = 0;
                    for (; i < count; ++i) {
                        ControllerProfile profile{{}, g_menuConfig.controllerProfiles[0]};
                        if (!ReadString(ifs, profile.name) || !Read(ifs, buttons) || buttons > k_maxEntries) break;
                        bool ok = true;
                        for (uint32_t b = 0; b < buttons && ok; ++b) {
                            int8_t button;
                            ok = Read(ifs, button);
                            if (ok && b < (uint32_t)k_profileActionCount && button >= CGamepadMapping::None && button <= CGamepadMapping::Y)
                                ProfileButton(profile.buttons, (int)b) = (CGamepadMapping::eButton)button;
                        }
                        if (!ok) break;
                        if (version >= 7) {
                            int8_t builtin;
                            if (!Read(ifs, builtin)) break;
                            if (builtin >= 0 && builtin < MenuConfig::k_profileCount) profile.builtin = builtin;
                        } else {
                            // older files: a profile still under a built-in name is that profile
                            for (int b = 0; b < MenuConfig::k_profileCount; ++b)
                                if (profile.name == k_defaultProfileNames[b]) profile.builtin = b;
                        }
                        s_profiles.push_back(std::move(profile));
                    }
                    profilesRead = i == count;
                }
                bool charactersRead = false;
                if (profilesRead && Read(ifs, count) && count <= k_maxEntries) {
                    uint32_t i = 0;
                    for (; i < count; ++i) {
                        Character character;
                        if (!ReadString(ifs, character.name) || !ReadValues(ifs, character.values) ||
                            (version >= 5 && !ReadString(ifs, character.playerName))) break;
                        if (version >= 8) {
                            int8_t game;
                            if (!Read(ifs, game)) break;
                            character.game = (game >= 0 && game < ArmorGameCount) ? game : ArmorReach;
                        } else {
                            // one armor set for every game: keep the game most of its items are from
                            auto split = SplitByGame(character.values);
                            int best = ArmorReach, best_count = -1;
                            for (int g = 0; g < ArmorGameCount; ++g) {
                                int count = 0;
                                for (int v = 0; v < k_serviceTagValue && v < (int)split[g].size(); ++v)
                                    count += split[g][v] >= 0;
                                if (count > best_count) { best = g; best_count = count; }
                            }
                            character.game = best;
                            character.values = split[best];
                        }
                        s_characters.push_back(std::move(character));
                    }
                    charactersRead = i == count;
                }
                bool armorRead = false;
                if (version >= 8 && charactersRead && Read(ifs, count) && count <= 4) {
                    uint32_t games = 0;
                    bool ok = Read(ifs, games) && games <= 64;
                    for (uint32_t p = 0; p < count && ok; ++p)
                        for (uint32_t g = 0; g < games && ok; ++g) {
                            std::vector<int> armor;
                            ok = ReadValues(ifs, armor);
                            if (ok && g < (uint32_t)ArmorGameCount) s_armor[p][g] = std::move(armor);
                        }
                    armorRead = ok;
                } else if (version >= 4 && charactersRead && Read(ifs, count) && count <= 4) {
                    // one armor set per player for every game: split it by the game each item is from
                    uint32_t p = 0;
                    for (; p < count; ++p) {
                        std::vector<int> armor;
                        if (!ReadValues(ifs, armor)) break;
                        auto split = SplitByGame(armor);
                        for (int g = 0; g < ArmorGameCount; ++g)
                            if (HasValues(split[g])) s_armor[p][g] = std::move(split[g]);
                    }
                    armorRead = p == count;
                }
                bool namesRead = false;
                if (version >= 6 && armorRead && Read(ifs, count) && count <= 4) {
                    uint32_t p = 0;
                    for (; p < count; ++p)
                        if (!ReadString(ifs, s_playerNames[p])) break;
                    namesRead = p == count;
                }
                if (version >= 9 && namesRead)
                    kvRead = ReadKeyValues(ifs, kv);
            }
        }
        if (kvRead)
            ApplyKeyValues(kv);
        else if (stateRead)
            MigrateToMccDefaults(s_state);

        // First run, or a file from before the menus were united: the six
        // built-in profiles (with any overrides from alpha_ring_menu.cfg)
        // become the starting custom profiles.
        if (s_profiles.empty())
            SeedProfiles();
        for (int& profile : s_state.controllerProfile)
            if (profile < -1 || profile >= (int)s_profiles.size()) profile = 0;
    }

    static std::string ToUtf8(const wchar_t* text) {
        std::string result;
        int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        if (size > 1) {
            result.resize(size - 1);
            WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
        }
        return result;
    }

    // Into a buffer of `size` characters; a name too long keeps what fit.
    static void FromUtf8(const std::string& text, wchar_t* out, int size) {
        if (MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out, size) == 0)
            out[size - 1] = L'\0';
    }

    static void ApplyPlayerNames() {
        for (int i = 0; i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile || s_playerNames[i].empty()) continue;
            FromUtf8(s_playerNames[i], profile->name, (int)std::size(profile->name));
        }
    }

    static void EnsureLoaded() {
        std::lock_guard lock(s_mutex);
        if (s_loaded) return;
        s_loaded = true;
        Load();
        ApplyPlayerNames();
    }

    void Initialize() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();

        // The saved player count, override and per-player controls have to be in
        // place before the first match is set up: H2A and Halo 4 take their player
        // list then, and a second player added once the match is running never
        // joins it (it ends at once, the player placing "-1th"). Only memory writes
        // here - teams and colors need the game engine, which doesn't exist yet,
        // and wait for game_setup.
        ApplySplitscreen(true);
        for (int i = 0; i < 4; ++i) {
            ApplyPlayerControls(i);
            ApplyPlayerMapping(i);
        }
    }

    std::recursive_mutex& Mutex() { return s_mutex; }

    MenuState& State() {
        EnsureLoaded();
        return s_state;
    }

    std::vector<ControllerProfile>& Profiles() {
        EnsureLoaded();
        return s_profiles;
    }

    std::vector<Character>& Characters() {
        EnsureLoaded();
        return s_characters;
    }

    bool Save() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        // the change reaches the game whether or not the file can be written
        Publish();
        if (s_unreadable)
            return false;

        // Written next to the file and moved over it, so a crash mid-write
        // can't leave a truncated file behind.
        const std::string& path = FilePath();
        std::string tmp = path + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs) {
                LOG_WARNING("MenuSettings: can't write {}", tmp);
                return false;
            }
            Write(ofs, k_magic);
            Write(ofs, k_version);
            WriteMenuState(ofs, s_state);

            Write(ofs, (uint32_t)s_profiles.size());
            for (auto& profile : s_profiles) {
                WriteString(ofs, profile.name);
                Write(ofs, (uint32_t)k_profileActionCount);
                for (int b = 0; b < k_profileActionCount; ++b)
                    Write(ofs, (int8_t)ProfileButton(profile.buttons, b));
                Write(ofs, (int8_t)profile.builtin);
            }

            Write(ofs, (uint32_t)s_characters.size());
            for (auto& character : s_characters) {
                WriteString(ofs, character.name);
                Write(ofs, (uint32_t)character.values.size());
                for (int value : character.values)
                    Write(ofs, (int32_t)value);
                WriteString(ofs, character.playerName);
                Write(ofs, (int8_t)character.game);
            }

            Write(ofs, (uint32_t)std::size(s_armor));
            Write(ofs, (uint32_t)ArmorGameCount);
            for (auto& player : s_armor)
                for (auto& armor : player) {
                    Write(ofs, (uint32_t)armor.size());
                    for (int value : armor)
                        Write(ofs, (int32_t)value);
                }

            Write(ofs, (uint32_t)std::size(s_playerNames));
            for (auto& name : s_playerNames)
                WriteString(ofs, name);

            WriteKeyValues(ofs, CollectKeyValues());

            if (!ofs.good()) return false;
        }
        if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            LOG_WARNING("MenuSettings: can't replace {} ({})", path, GetLastError());
            return false;
        }
        return true;
    }

    bool ResetAll() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        if (MCC::IsInGame())
            return false;
        const char* files[] = {"alpha_ring_menu.bin", "alpha_ring_controllers.cfg", "alpha_ring_patches.cfg",
                               "alpha_ring_splitscreen.cfg"};
        for (const char* name : files) {
            std::string path = AlphaRing::Filesystem::ModuleFile(name);
            if (!DeleteFileA(path.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND)
                LOG_WARNING("MenuSettings: can't delete {} ({})", path, GetLastError());
            else
                LOG_INFO("MenuSettings: deleted {}", path);
        }
        s_unreadable = false;
        Load(); // no file: the defaults
        for (int i = 0; i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile) continue;
            swprintf(profile->name, std::size(profile->name), L"Player %d", i + 1);
            profile->controller_index = s_state.controllerIndex[i];
        }
        ApplySplitscreen(true); // publishes
        AlphaRing::Input::PlayStation::ForgetSlots();
        return true;
    }

    // ---------------------------------------------------------------- published copy
    //
    // The game asks for a player's profile and button layout from its own
    // threads, often, and while holding its own locks; the menus hold Mutex()
    // while calling into the game. So the game hooks never take Mutex(): they
    // read this copy of what they need, which Publish() replaces under a lock
    // held only for the copy.

    struct PublishedPlayer {
        int sensitivity;   // 0 = MCC's
        int invert;        // -1 = MCC's
        int invertFlight;
        int vibration;
        int deadzone;      // percent
        int profile;       // -1 = MCC's layout
        ControllerProfileMapping buttons;
        int namedColor[3]; // AlphaRing colors, -1 = none
        int lookColors[k_gameCount][k_lookColorCount];
        int armor[ArmorGameCount][k_armorValueCount];
    };

    struct Published {
        bool valid;
        bool shareLook;      // players 2-4 wear player 1's look
        bool anyoneCanPause;
        PublishedPlayer players[4];
    };

    static SRWLOCK s_publishLock = SRWLOCK_INIT;
    static Published s_published{};

    void Publish() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        auto pub = std::make_unique<Published>();
        auto& ms = s_state;
        pub->valid = true;
        pub->shareLook = ms.usePlayer0Profile;
        pub->anyoneCanPause = ms.anyoneCanPause;
        for (int i = 0; i < 4; ++i) {
            auto& p = pub->players[i];
            p.sensitivity = ms.sensitivity[i];
            p.invert = ms.invert[i];
            p.invertFlight = ms.invertFlight[i];
            p.vibration = ms.vibration[i];
            p.deadzone = std::clamp(ms.deadzone[i], 0, k_maxDeadzone);
            p.profile = (ms.controllerProfile[i] >= 0 && ms.controllerProfile[i] < (int)s_profiles.size()) ? ms.controllerProfile[i] : -1;
            p.buttons = p.profile >= 0 ? s_profiles[p.profile].buttons : g_menuConfig.controllerProfiles[0];
            for (int c = 0; c < 3; ++c) p.namedColor[c] = ms.playerColors[i].colors[c];
            for (int g = 0; g < k_gameCount; ++g)
                for (int k = 0; k < k_lookColorCount; ++k)
                    p.lookColors[g][k] = k < (int)s_lookColors[i][g].size() ? s_lookColors[i][g][k] : -1;
            for (int g = 0; g < ArmorGameCount; ++g) {
                auto& values = s_armor[i][g];
                for (int v = 0; v < k_armorValueCount; ++v)
                    p.armor[g][v] = v < (int)values.size() ? values[v] : (v >= k_serviceTagValue ? 0 : -1);
            }
        }
        AcquireSRWLockExclusive(&s_publishLock);
        s_published = *pub;
        ReleaseSRWLockExclusive(&s_publishLock);

        // the PlayStation lightbars show whose controller each one is
        AlphaRing::Input::PlayStation::SetEnabled(ms.nativePlayStation);
        int slotPlayer[4] = {-1, -1, -1, -1};
        for (int i = (std::min)(ms.playerCount, 4) - 1; i >= 0; --i) {
            if (i == 0 && ms.useKM) continue;
            int slot = ms.controllerIndex[i];
            if (slot >= 0 && slot < 4) slotPlayer[slot] = i;
        }
        for (int slot = 0; slot < 4; ++slot)
            AlphaRing::Input::PlayStation::SetSlotPlayer(slot, slotPlayer[slot]);
    }

    static bool ReadPublished(int player, PublishedPlayer& out, PublishedPlayer* lookOut, bool* shareLook) {
        if (player < 0 || player >= 4) return false;
        AcquireSRWLockShared(&s_publishLock);
        bool valid = s_published.valid;
        if (valid) {
            out = s_published.players[player];
            if (shareLook) *shareLook = s_published.shareLook && player > 0;
            if (lookOut) *lookOut = s_published.players[0];
        }
        ReleaseSRWLockShared(&s_publishLock);
        return valid;
    }

    bool AnyoneCanPause() {
        AcquireSRWLockShared(&s_publishLock);
        bool result = s_published.valid && s_published.anyoneCanPause;
        ReleaseSRWLockShared(&s_publishLock);
        return result;
    }

    int StickDeadzone(int player) {
        if (player < 0 || player >= 4) return 0;
        AcquireSRWLockShared(&s_publishLock);
        int result = s_published.valid ? s_published.players[player].deadzone : 0;
        ReleaseSRWLockShared(&s_publishLock);
        return result;
    }

    bool VibrationOff(int player) {
        if (player < 0 || player >= 4) return false;
        AcquireSRWLockShared(&s_publishLock);
        bool result = s_published.valid && s_published.players[player].vibration == 0;
        ReleaseSRWLockShared(&s_publishLock);
        return result;
    }

    // `values` (a look's armor, Armor() layout) over `profile`, for ArmorGame `game`.
    static bool LayArmor(const int* values, int game, CUserProfile& profile) {
        bool changed = false;
        for (int v = 1; v < k_serviceTagValue; ++v) {
            if (values[v] < 0) continue;
            // never an item from another game, whatever the file holds
            if (IsItemValue(v) && ArmorGameOfItem(values[v]) != game) continue;
            profile.*(k_characterFields[v - 1].field) = values[v];
            changed = true;
        }
        if (values[k_serviceTagValue] != 0) {
            for (int i = 0; i < k_serviceTagLength; ++i)
                profile.ServiceTag[i] = (wchar_t)values[k_serviceTagValue + i];
            changed = true;
        }
        return changed;
    }

    static void ColorFields(CUserProfile& p, int* (&fields)[k_lookColorCount]) {
        fields[0] = &p.PlayerModelPrimaryColorIndex;
        fields[1] = &p.PlayerModelSecondaryColorIndex;
        fields[2] = &p.PlayerModelTertiaryColorIndex;
        fields[3] = &p.PlayerModelPrimaryColor;
        fields[4] = &p.PlayerModelSecondaryColor;
        fields[5] = &p.PlayerModelTertiaryColor;
    }

    // A look: copied armor, then copied colors, then colors picked in a menu.
    static bool LayLook(const PublishedPlayer& look, int game, CUserProfile& profile) {
        bool changed = false;
        int armorGame = ArmorGameOf(game);
        if (armorGame >= 0)
            changed |= LayArmor(look.armor[armorGame], armorGame, profile);
        int* fields[k_lookColorCount];
        ColorFields(profile, fields);
        if (game >= 0 && game < k_gameCount) {
            for (int k = 0; k < k_lookColorCount; ++k) {
                if (look.lookColors[game][k] < 0) continue;
                *fields[k] = look.lookColors[game][k];
                changed = true;
            }
            for (int c = 0; c < 3; ++c) {
                int color = look.namedColor[c];
                if (color < 0 || color >= CXboxColorMapping::kColorCount) continue;
                *fields[c] = CXboxColorMapping::GetColorIndex(color, game);
                *fields[c + 3] = CXboxColorMapping::GetColorItemIndex(color, game);
                changed = true;
            }
        }
        return changed;
    }

    // The look MCC fills into its profile after the match-start snapshot:
    // the colors in every game, and the armor block in the games that have
    // one (CE, Halo 2 and ODST have no armor; theirs stays the snapshot's).
    static void TakeLiveLook(const CUserProfile& live, CUserProfile& p, int game) {
        auto copy = [&](size_t from, size_t to) {
            memcpy(reinterpret_cast<char*>(&p) + from, reinterpret_cast<const char*>(&live) + from, to - from);
        };
        copy(offsetof(CUserProfile, PlayerModelPrimaryColorIndex), offsetof(CUserProfile, UseEliteModel));
        copy(offsetof(CUserProfile, PlayerModelPrimaryColor), offsetof(CUserProfile, SpartanPose));
        if (ArmorGameOf(game) >= 0) {
            copy(offsetof(CUserProfile, UseEliteModel), offsetof(CUserProfile, LockMaxAspectRatio));
            copy(offsetof(CUserProfile, PlayerModelPermutation), offsetof(CUserProfile, OnlineMedalFlasher));
        }
    }

    bool ComposeProfile(int player, const CUserProfile& snapshot, const CUserProfile* live, CUserProfile& out, int game) {
        if (&out != &snapshot)
            memcpy(&out, &snapshot, sizeof(CUserProfile));
        PublishedPlayer p, first;
        bool shareLook = false;
        if (!ReadPublished(player, p, &first, &shareLook))
            return false;
        bool fromLive = live && live != &snapshot;
        bool changed = false;

        if (fromLive) {
            TakeLiveLook(*live, out, game);
            changed = true;
        }
        changed |= LayLook(shareLook ? first : p, game, out);

        if (p.sensitivity > 0) {
            out.VerticalLookSensitivity = (unsigned char)p.sensitivity;
            out.HorizontalLookSensitivity = (unsigned char)p.sensitivity;
            changed = true;
        } else if (fromLive) {
            out.VerticalLookSensitivity = live->VerticalLookSensitivity;
            out.HorizontalLookSensitivity = live->HorizontalLookSensitivity;
        }
        if (p.invert >= 0) {
            out.LookControlsInverted = p.invert == 1;
            out.MouseLookControlsInverted = p.invert == 1;
            changed = true;
        } else if (fromLive) {
            out.LookControlsInverted = live->LookControlsInverted;
            out.MouseLookControlsInverted = live->MouseLookControlsInverted;
        }
        if (p.invertFlight >= 0) {
            out.AircraftControlsInverted = p.invertFlight == 1;
            out.MouseAircraftControlsInverted = p.invertFlight == 1;
            changed = true;
        } else if (fromLive) {
            out.AircraftControlsInverted = live->AircraftControlsInverted;
            out.MouseAircraftControlsInverted = live->MouseAircraftControlsInverted;
        }
        if (p.vibration >= 0) {
            out.VibrationDisabled = p.vibration == 0;
            out.ImpulseTriggersDisabled = p.vibration == 0;
            changed = true;
        } else if (fromLive) {
            out.VibrationDisabled = live->VibrationDisabled;
            out.ImpulseTriggersDisabled = live->ImpulseTriggersDisabled;
        }
        return changed;
    }

    // Actions outside a controller profile that are used on foot, where a
    // button a profile moves onto them would do two things at once. Vehicle,
    // Forge and Theater actions share buttons with on-foot ones by design.
    static bool IsOnFootAction(int action) {
        switch (action) {
            case 11: case 12: case 14: case 55: case 56: case 57: case 58:
            case 59: case 60: case 61: case 64: case 65:
                return true;
            default:
                return false;
        }
    }

    static void LayProfile(const ControllerProfileMapping& buttons, const CGamepadMapping& base, CGamepadMapping& mapping) {
        // the buttons the profile puts on an action that MCC has elsewhere
        bool moved[17] = {};
        for (auto& binding : k_profileBindings) {
            auto button = buttons.*binding.field;
            mapping.actions[binding.action] = button;
            if (button != CGamepadMapping::None && button != base.actions[binding.action] && button >= 0 && button < 16)
                moved[button] = true;
        }
        // Everything else keeps MCC's button (Sprint, loadouts, vehicle and
        // Forge actions...), unless the profile now uses that button on foot.
        for (int a = 0; a < (int)std::size(mapping.actions); ++a) {
            if (IsProfileAction(a) || !IsOnFootAction(a)) continue;
            auto button = base.actions[a];
            if (button >= 0 && button < 16 && moved[button])
                mapping.actions[a] = CGamepadMapping::None;
        }
        // Player Move Forward/Backward/Left/Right (H1A, actions 16-19) stay
        // unbound — movement is handled by the analog stick, not the D-Pad.
        for (int a = 16; a <= 19; ++a)
            mapping.actions[a] = CGamepadMapping::None;
    }

    bool ComposeMapping(int player, const CGamepadMapping& base, CGamepadMapping& out) {
        if (&out != &base)
            memcpy(&out, &base, sizeof(CGamepadMapping));
        PublishedPlayer p;
        if (!ReadPublished(player, p, nullptr, nullptr) || p.profile < 0)
            return false;
        LayProfile(p.buttons, base, out);
        return true;
    }

    // ---------------------------------------------------------------- apply

    void ApplyControllerProfile(int profileIndex, CGamepadMapping& mapping) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (profileIndex < 0 || profileIndex >= (int)profiles.size())
            return; // MCC's layout: as it is
        CGamepadMapping base;
        memcpy(&base, &mapping, sizeof(base));
        LayProfile(profiles[profileIndex].buttons, base, mapping);
    }

    bool IsProfileAction(int action) {
        return std::any_of(std::begin(k_profileBindings), std::end(k_profileBindings),
                           [action](const auto& binding) { return binding.action == action; });
    }

    void ApplyPlayerControls(int player) {
        std::lock_guard lock(s_mutex);
        if (auto profile = CGameManager::get_profile(player))
            profile->controller_index = State().controllerIndex[player];
        Publish();
    }

    void ApplyPlayerMapping(int player) {
        Publish();
    }

    void ApplyPlayerTeam(int player) {
        std::lock_guard lock(s_mutex);
        if (player >= State().playerCount) {
            MATCH_LOG("change_team player={} skipped: not in the match (player_count={})", player, State().playerCount);
            return;
        }
        auto engine = GameEngine();
        auto xuid = CGameManager::get_xuid(player);
        MATCH_LOG("change_team player={} xuid={:#x} team={}{}", player, (unsigned long long)xuid,
                  State().teamIndex[player], (xuid && engine) ? "" : " (skipped: no xuid/engine)");
        if (xuid && engine)
            engine->change_team(xuid, State().teamIndex[player]);
    }

    void ApplyPlayerColors(int player) {
        Publish();
    }

    void ApplySplitscreen(bool atGameStart) {
        std::lock_guard lock(s_mutex);
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        if (!p_setting) return;
        auto& ms = State();
        if (atGameStart)
            p_setting->b_override = ms.playerCount > 1;
        else if (ms.playerCount > 1)
            p_setting->b_override = true;
        p_setting->player_count = ms.playerCount;
        p_setting->b_player0_use_km = ms.useKM;
        p_setting->b_use_player0_profile = ms.usePlayer0Profile;
        p_setting->b_halo2_composite_clip = ms.halo2CompositeClip;
        p_setting->b_classic_graphics_3p = ms.classicGraphics3P;
        MATCH_LOG("ApplySplitscreen(atGameStart={}): override={} player_count={} p1_km={} use_p1_look={}",
                  atGameStart, p_setting->b_override, p_setting->player_count, p_setting->b_player0_use_km,
                  p_setting->b_use_player0_profile);
        Publish();
    }

    static bool TeamChangeLeavesMatch() {
        auto* gg = GameGlobal();
        return gg && (gg->current_game == CGameGlobal::Halo4 || gg->current_game == CGameGlobal::GroundHog);
    }

    void ApplyAll(bool atGameStart, const int* previousTeams) {
        std::lock_guard lock(s_mutex);
        for (int i = 0; i < 4; ++i)
            if (auto profile = CGameManager::get_profile(i))
                profile->controller_index = State().controllerIndex[i];
        ApplySplitscreen(atGameStart); // publishes
        bool picked_only = TeamChangeLeavesMatch();
        for (int i = 0; i < 4; ++i) {
            if (!picked_only || (previousTeams && previousTeams[i] != State().teamIndex[i]))
                ApplyPlayerTeam(i);
            else
                MATCH_LOG("change_team player={} held back: Halo 4/H2A take it once the match is running", i);
        }
    }

    // ---------------------------------------------------------------- profiles

    const char* ProfileActionName(int action) {
        return (action >= 0 && action < k_profileActionCount) ? k_profileActions[action].name : "";
    }

    CGamepadMapping::eButton& ProfileButton(ControllerProfileMapping& buttons, int action) {
        return buttons.*(k_profileActions[std::clamp(action, 0, k_profileActionCount - 1)].field);
    }

    int AddProfile(const std::string& name, const ControllerProfileMapping& buttons) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        profiles.push_back({name, buttons});
        return (int)profiles.size() - 1;
    }

    // The first CGamepadMapping action a profile action sets (its mirrors follow it).
    static int MappingActionOf(int profileAction) {
        auto field = k_profileActions[std::clamp(profileAction, 0, k_profileActionCount - 1)].field;
        for (auto& binding : k_profileBindings)
            if (binding.field == field) return binding.action;
        return -1;
    }

    ControllerProfileMapping MccButtons() {
        ControllerProfileMapping buttons = g_menuConfig.controllerProfiles[0];
        auto* mng = GameManager();
        if (!mng || !MCC::IsInGame()) return buttons;
        auto xuid = CGameManager::get_xuid(0);
        auto* base = xuid ? CGameManager::ppOriginal.retrive_gamepad_mapping(mng, xuid) : nullptr;
        if (!base) return buttons;
        for (int a = 0; a < k_profileActionCount; ++a) {
            int action = MappingActionOf(a);
            if (action >= 0) ProfileButton(buttons, a) = base->actions[action];
        }
        return buttons;
    }

    int OwnProfile(int player, MenuState& ms) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (player < 0 || player >= 4) return -1;
        int current = ms.controllerProfile[player];
        bool shared = false;
        for (int i = 0; i < 4; ++i)
            shared |= i != player && ms.controllerProfile[i] == current;
        if (current >= 0 && current < (int)profiles.size() && profiles[current].builtin < 0 && !shared)
            return current;

        ControllerProfileMapping buttons = (current >= 0 && current < (int)profiles.size()) ? profiles[current].buttons : MccButtons();
        std::string base;
        auto* profile = CGameManager::get_profile(player);
        base = profile && profile->name[0] ? ToUtf8(profile->name) : "Player " + std::to_string(player + 1);
        if (base.size() > 64) base.resize(64);
        base += "'s Controls";
        std::string name = base;
        for (int n = 2; std::any_of(profiles.begin(), profiles.end(), [&](const ControllerProfile& p) { return p.name == name; }); ++n)
            name = base + " " + std::to_string(n);
        int index = AddProfile(name, buttons);
        ms.controllerProfile[player] = index;
        return index;
    }

    void DeleteProfile(int index) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (index < 0 || index >= (int)profiles.size() || profiles.size() <= 1)
            return;
        profiles.erase(profiles.begin() + index);
        for (int i = 0; i < 4; ++i) {
            int& selected = State().controllerProfile[i];
            if (selected == index) {
                selected = 0;
            } else if (selected > index) {
                --selected;
            }
        }
        Publish();
    }

    void ResetProfile(int index) {
        std::lock_guard lock(s_mutex);
        auto& profiles = Profiles();
        if (index < 0 || index >= (int)profiles.size()) return;
        int builtin = profiles[index].builtin;
        profiles[index].buttons = g_menuConfig.controllerProfiles[builtin >= 0 ? builtin : 0];
        ReapplyProfile(index);
    }

    void ReapplyProfile(int index) {
        Publish();
    }

    // ---------------------------------------------------------------- characters

    void SaveAll() {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        for (int i = 0; i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile) continue;
            std::string name = ToUtf8(profile->name);
            if (name.size() > k_maxString) name.resize(k_maxString);
            s_playerNames[i] = std::move(name);
        }
        Save();
    }

    std::string CharacterName(const wchar_t* playerName) {
        std::string name = ToUtf8(playerName);
        if (name.size() > k_maxString - 16) name.resize(k_maxString - 16);
        return name + "'s Character";
    }

    const char* ArmorGameName(int game) {
        return (game >= 0 && game < ArmorGameCount) ? k_armorGameNames[game] : "";
    }

    int ArmorItemGame(int game) {
        return (game >= 0 && game < ArmorGameCount) ? k_armorItemGames[game] : CUserProfile::ItemOther;
    }

    int CurrentArmorGame() {
        auto* gg = GameGlobal();
        if (!gg) return -1;
        switch (gg->current_game) {
            case CGameGlobal::GroundHog: return ArmorHalo2A;
            case CGameGlobal::Halo3:     return ArmorHalo3;
            case CGameGlobal::HaloReach: return ArmorReach;
            case CGameGlobal::Halo4:     return ArmorHalo4;
            default:                     return -1;
        }
    }

    int CurrentGame() {
        auto* gg = GameGlobal();
        if (!gg || !MCC::IsInGame()) return -1;
        int game = (int)gg->current_game;
        return (game >= 0 && game < k_gameCount) ? game : -1;
    }

    int ArmorGameOf(int game) {
        switch (game) {
            case CGameGlobal::GroundHog: return ArmorHalo2A;
            case CGameGlobal::Halo3:     return ArmorHalo3;
            case CGameGlobal::HaloReach: return ArmorReach;
            case CGameGlobal::Halo4:     return ArmorHalo4;
            default:                     return -1;
        }
    }

    int GameOfArmorGame(int armorGame) {
        static const int games[ArmorGameCount] = {CGameGlobal::GroundHog, CGameGlobal::Halo3, CGameGlobal::HaloReach, CGameGlobal::Halo4};
        return (armorGame >= 0 && armorGame < ArmorGameCount) ? games[armorGame] : -1;
    }

    std::vector<int>& LookColors(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        auto& colors = s_lookColors[std::clamp(player, 0, 3)][std::clamp(game, 0, k_gameCount - 1)];
        if ((int)colors.size() < k_lookColorCount)
            colors.resize(k_lookColorCount, -1);
        return colors;
    }

    bool CaptureLook(int player, MenuState* ms) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        int game = CurrentGame();
        auto* mng = GameManager();
        if (player < 0 || player >= 4 || game < 0 || !mng) return false;
        auto xuid = CGameManager::get_xuid(0);
        auto* live = xuid ? CGameManager::ppOriginal.get_player_profile(mng, xuid) : nullptr;
        if (!live) return false;
        CUserProfile base;
        memcpy(&base, live, sizeof(base));

        int* fields[k_lookColorCount];
        ColorFields(base, fields);
        auto& colors = LookColors(player, game);
        for (int k = 0; k < k_lookColorCount; ++k)
            colors[k] = *fields[k];

        int armorGame = ArmorGameOf(game);
        if (armorGame >= 0) {
            auto armor = EmptyArmor();
            for (int v = 1; v < k_serviceTagValue; ++v) {
                int value = base.*(k_characterFields[v - 1].field);
                if (IsItemValue(v) && (value < 0 || ArmorGameOfItem(value) != armorGame)) continue;
                armor[v] = value;
            }
            for (int i = 0; i < k_serviceTagLength; ++i)
                armor[k_serviceTagValue + i] = (int)base.ServiceTag[i];
            s_armor[player][armorGame] = std::move(armor);
        }

        auto& picked = (ms ? *ms : s_state).playerColors[player].colors;
        picked[0] = picked[1] = picked[2] = -1;
        if (ms != &s_state) s_state.playerColors[player] = (ms ? *ms : s_state).playerColors[player];
        LOG_INFO("MenuSettings: player {} took the MCC look of game {} (colors {} {} {})", player + 1, game,
                 colors[0], colors[1], colors[2]);
        Publish();
        return true;
    }

    void ClearLook(int player, MenuState* ms) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        if (player < 0 || player >= 4) return;
        int game = CurrentGame();
        for (int g = 0; g < k_gameCount; ++g)
            if (game < 0 || g == game) s_lookColors[player][g].clear();
        for (int g = 0; g < ArmorGameCount; ++g)
            if (game < 0 || GameOfArmorGame(g) == game) s_armor[player][g].clear();
        auto& picked = (ms ? *ms : s_state).playerColors[player].colors;
        picked[0] = picked[1] = picked[2] = -1;
        s_state.playerColors[player] = (ms ? *ms : s_state).playerColors[player];
        Publish();
    }

    bool HasLook(int player) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        int game = CurrentGame();
        if (player < 0 || player >= 4 || game < 0) return false;
        for (int c : s_lookColors[player][game]) if (c >= 0) return true;
        return HasArmor(player, ArmorGameOf(game));
    }

    std::vector<int>& Armor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        auto& armor = s_armor[std::clamp(player, 0, 3)][std::clamp(game, 0, ArmorGameCount - 1)];
        if ((int)armor.size() < k_armorValueCount) {
            auto full = EmptyArmor();
            std::copy(armor.begin(), armor.end(), full.begin());
            armor = std::move(full);
        }
        return armor;
    }

    int ArmorValueIndex(int CUserProfile::* field) {
        for (int i = 0; i < (int)std::size(k_characterFields); ++i)
            if (k_characterFields[i].field == field) return i + 1;
        return -1;
    }

    int ArmorServiceTagIndex() { return k_serviceTagValue; }

    bool HasArmor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        return player >= 0 && player < 4 && game >= 0 && game < ArmorGameCount && HasValues(s_armor[player][game]);
    }

    bool HasArmor(int player) {
        return HasArmor(player, CurrentArmorGame());
    }

    void ClearArmor(int player, int game) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        if (player >= 0 && player < 4 && game >= 0 && game < ArmorGameCount) {
            s_armor[player][game].clear();
            int g = GameOfArmorGame(game);
            if (g >= 0) s_lookColors[player][g].clear();
        }
        Publish();
    }

    void ApplyArmor(int player, CUserProfile& profile) {
        std::lock_guard lock(s_mutex);
        EnsureLoaded();
        int game = CurrentArmorGame();
        if (player < 0 || player >= 4 || game < 0)
            return;
        const auto& values = s_armor[player][game];
        for (int v = 1; v < (int)values.size() && v < k_serviceTagValue; ++v) {
            if (values[v] < 0) continue;
            // never an item from another game, whatever the file holds
            if (IsItemValue(v) && ArmorGameOfItem(values[v]) != game) continue;
            profile.*(k_characterFields[v - 1].field) = values[v];
        }
        if ((int)values.size() >= k_armorValueCount && values[k_serviceTagValue] != 0)
            for (int i = 0; i < k_serviceTagLength; ++i)
                profile.ServiceTag[i] = (wchar_t)values[k_serviceTagValue + i];
    }

    int SaveCharacter(const std::string& name, int game, const std::vector<int>& armor, const wchar_t* playerName,
                      const std::vector<int>* colors) {
        std::lock_guard lock(s_mutex);
        Character character{name, armor, ToUtf8(playerName), game};
        if (colors)
            for (int k = 0; k < k_lookColorCount && k < (int)colors->size(); ++k)
                character.colors[k] = (*colors)[k];

        auto& characters = Characters();
        for (int i = 0; i < (int)characters.size(); ++i) {
            if (characters[i].name == name && characters[i].game == game) {
                characters[i] = std::move(character);
                return i;
            }
        }
        characters.push_back(std::move(character));
        return (int)characters.size() - 1;
    }

    void LoadCharacter(int index, int player, wchar_t* playerName, int playerNameSize) {
        std::lock_guard lock(s_mutex);
        auto& characters = Characters();
        if (index < 0 || index >= (int)characters.size() || player < 0 || player >= 4)
            return;
        const auto& character = characters[index];
        auto& armor = Armor(player, character.game);
        armor = EmptyArmor();
        std::copy_n(character.values.begin(), (std::min)(character.values.size(), armor.size()), armor.begin());
        int game = GameOfArmorGame(character.game);
        if (game >= 0) {
            auto& colors = LookColors(player, game);
            for (int k = 0; k < k_lookColorCount; ++k)
                colors[k] = k < (int)character.colors.size() ? character.colors[k] : -1;
        }
        if (!character.playerName.empty() && playerName && playerNameSize > 0)
            FromUtf8(character.playerName, playerName, playerNameSize);
        Publish();
    }

    void DeleteCharacter(int index) {
        std::lock_guard lock(s_mutex);
        auto& characters = Characters();
        if (index >= 0 && index < (int)characters.size())
            characters.erase(characters.begin() + index);
    }
}
