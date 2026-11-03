#include "Splitscreen.h"

#include "common.h"

#include "global/Global.h"

#include <offset_mcc.h>

#include "../CGameManager.h"
#include "../CGameGlobal.h"
#include "log/MatchLog.h"

#include <atomic>
#include <string>

namespace Halo2::Entry::Anniversary { bool Available(); std::string Status(); }

namespace MCC::Splitscreen {
    DefDetourFunction(__int64, __fastcall, get_index_by_xuid, void* a1, __int64 xuid) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();

        __int64 result = p_setting->b_override ? CGameManager::get_index(xuid) : ppOriginal_get_index_by_xuid(a1, xuid);

        // once per match per answer
        static std::atomic<long> s_seen[5];
        int slot = (result >= 0 && result < 4) ? (int)result : 4;
        if (AlphaRing::MatchLog::FirstThisMatch(s_seen[slot]))
            MATCH_LOG("get_index_by_xuid xuid={:#x} -> {} override={} (caller {})", (unsigned long long)xuid, result,
                      p_setting->b_override, AlphaRing::MatchLog::Describe(_ReturnAddress()));
        return result;
    }

    // todo:: let other players have the ability to pause the game

    bool Initialize() {
        bool result;

        // fix: changing team freeze the game
        result = AlphaRing::Hook::Detour({
            {OFFSET_MCC_PF_GET_INDEX_BY_XUID, OFFSET_MCC_WS_PF_GET_INDEX_BY_XUID, get_index_by_xuid, (void**)&ppOriginal_get_index_by_xuid},
        });

        assertm(result, "MCC:Splitscreen: failed to hook");

        return true;
    }

    static std::atomic<bool> s_anniversary_quad_active{false}; // the current mission's

    bool AnniversaryQuadChosen() { return AlphaRing::Global::MCC::Splitscreen()->b_anniversary_quad; }
    bool AnniversaryQuadActive() { return s_anniversary_quad_active; }
    bool AnniversaryTwoViewFixChosen() { return AlphaRing::Global::MCC::Splitscreen()->b_halo2_two_view_fix; }

    ClassicGraphicsScope::ClassicGraphicsScope(unsigned char* game_options, int game) {
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        // The mode is set up for every Halo 2 mission it can be (it runs with 3-4 players while Anniversary is
        // on screen: Back switches to Anniversary in the middle of a mission).
        s_anniversary_quad_active = p_setting->b_override && game == CGameGlobal::Halo2 && AnniversaryQuadChosen() &&
                                    Halo2::Entry::Anniversary::Available();
        LOG_INFO("Splitscreen: {} mission starts: players={} override={} options={} anniversary_bit={} quad: chosen={} available={} -> active={}",
                 game == CGameGlobal::Halo1 ? "Halo CE" : "Halo 2", p_setting->player_count, p_setting->b_override,
                 game_options != nullptr, game_options ? (game_options[0] & 1) : -1, AnniversaryQuadChosen(),
                 game == CGameGlobal::Halo2 && Halo2::Entry::Anniversary::Available(), s_anniversary_quad_active.load());
        if (!p_setting->b_override || game_options == nullptr) return;
        if (p_setting->player_count <= 2) return;
        if (!(game_options[0] & 1)) return; // Classic already
        if (s_anniversary_quad_active) {
            LOG_INFO("Splitscreen: {} players in Halo 2 Anniversary graphics (experimental)", p_setting->player_count);
            return;
        }
        if (!p_setting->b_classic_graphics_3p) {
            LOG_WARNING("Splitscreen: {} players in {} Anniversary graphics, which draws two views only{}",
                        p_setting->player_count, game == CGameGlobal::Halo1 ? "Halo CE" : "Halo 2",
                        game == CGameGlobal::Halo2 && AnniversaryQuadChosen() ? " (the 3-4 player mode is missing an address in this build)" : "");
            return;
        }

        LOG_INFO("Splitscreen: {} players in {}: starting in Classic graphics (Anniversary draws two views only)",
                 p_setting->player_count, game == CGameGlobal::Halo1 ? "Halo CE" : "Halo 2");
        m_options = game_options;
        m_saved = game_options[0];
        game_options[0] &= ~1;
    }

    ClassicGraphicsScope::~ClassicGraphicsScope() {
        if (m_options) m_options[0] = m_saved;
    }
}

#include "imgui.h"
#include "mcc/mcc.h"
#include "input/Input.h"
#include "mcc/settings/MenuSettings.h"
#include "input/PlayStation.h"
#include "render/imgui/Toast.h"
#include "render/imgui/game/xbox/assets/colors.h"

#include <algorithm>
#include <string>

// Settings the Xbox menu (F4) also shows - player count, K/M, controller slot
// and profile, sensitivity, inversion, team and colors - live in
// AlphaRing::MenuSettings, so an edit here is written to the shared menu file
// and shows in the Xbox menu, and the other way round. Every frame of this
// window holds MenuSettings::Mutex().
namespace MCC::Splitscreen {
    namespace Settings = AlphaRing::MenuSettings;

    void RealContext();

    static void ReloadSettings() {
        if (auto p_engine = GameEngine())
            p_engine->load_setting();
    }

    // Detect which controller (0-3) has any button/trigger pressed, returns -1 if none
    static int DetectActiveController() {
        for (int i = 0; i < 4; i++) {
            XINPUT_STATE state;
            if (AlphaRing::Input::GetXInputGetState(i, &state)) {
                if (state.Gamepad.wButtons != 0 ||
                    state.Gamepad.bLeftTrigger > 30 ||
                    state.Gamepad.bRightTrigger > 30) {
                    return i;
                }
            }
        }
        return -1;
    }

    // Binding state: which player slot is waiting for controller input (-1 = none)
    static int s_binding_player = -1;

    void ImGuiContext() {
        static bool show_splitscreen;

        if (ImGui::BeginMainMenuBar()) {
            ImGui::MenuItem("Splitscreen", nullptr, &show_splitscreen);
            ImGui::EndMainMenuBar();
        }

        if (show_splitscreen) {
            if (ImGui::Begin("Splitscreen", &show_splitscreen, ImGuiWindowFlags_MenuBar)) {
                std::lock_guard lock(Settings::Mutex());
                RealContext();
            }
            ImGui::End();
        }
    }

    static bool ProfileCombo(const char* label, int* index) {
        auto& profiles = Settings::Profiles();
        const char* preview = (*index >= 0 && *index < (int)profiles.size()) ? profiles[*index].name.c_str() : "MCC Settings";
        bool changed = false;
        if (ImGui::BeginCombo(label, preview)) {
            if (ImGui::Selectable("MCC Settings", *index < 0)) {
                *index = -1;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The buttons set in MCC's own controller settings");
            for (int i = 0; i < (int)profiles.size(); ++i) {
                ImGui::PushID(i);
                if (ImGui::Selectable(profiles[i].name.c_str(), *index == i)) {
                    *index = i;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    static bool ColorCombo(const char* label, int* color) {
        bool changed = false;
        bool picked = *color >= 0 && *color < IM_ARRAYSIZE(k_colorNames);
        ImGui::PushID(label);
        ImGui::ColorButton("##swatch", picked ? ImGui::ColorConvertU32ToFloat4(defaultColors[*color]) : ImVec4(0, 0, 0, 0),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_AlphaPreview);
        ImGui::PopID();
        ImGui::SameLine();
        if (ImGui::BeginCombo(label, picked ? k_colorNames[*color] : "From my look / MCC")) {
            if (ImGui::Selectable("From my look / MCC", !picked)) {
                *color = -1;
                changed = true;
            }
            for (int c = 0; c < IM_ARRAYSIZE(k_colorNames); ++c) {
                ImGui::PushID(c);
                ImGui::ColorButton("##swatch", ImGui::ColorConvertU32ToFloat4(defaultColors[c]), ImGuiColorEditFlags_NoTooltip);
                ImGui::SameLine();
                if (ImGui::Selectable(k_colorNames[c], *color == c)) {
                    *color = c;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    // The per-player settings the Xbox menu has too.
    static void SharedPlayerContext(int index) {
        auto& ms = Settings::State();

        ImGui::PushItemWidth(200);

        if (ProfileCombo("Controller Profile", &ms.controllerProfile[index])) {
            Settings::ApplyPlayerMapping(index);
            ReloadSettings();
            Settings::Save();
        }

        if (ImGui::SliderInt("Look Sensitivity", &ms.sensitivity[index], 0, 20, ms.sensitivity[index] <= 0 ? "MCC" : "%d")) {
            Settings::ApplyPlayerControls(index);
            ReloadSettings();
        }
        if (ImGui::IsItemDeactivatedAfterEdit())
            Settings::Save();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("0 = the sensitivity set in MCC");

        // -1 = MCC's setting
        auto triState = [&](const char* label, int* value, const char* off, const char* on) {
            const char* items[] = {"MCC Setting", off, on};
            int selected = *value + 1;
            if (ImGui::Combo(label, &selected, items, IM_ARRAYSIZE(items))) {
                *value = selected - 1;
                Settings::ApplyPlayerControls(index);
                ReloadSettings();
                Settings::Save();
            }
        };
        triState("Look", &ms.invert[index], "Normal", "Inverted");
        triState("Flying", &ms.invertFlight[index], "Normal", "Inverted");
        triState("Vibration", &ms.vibration[index], "Off", "On");

        if (ImGui::SliderInt("Extra Stick Deadzone", &ms.deadzone[index], 0, Settings::k_maxDeadzone, ms.deadzone[index] ? "%d%%" : "Off"))
            Settings::Publish();
        if (ImGui::IsItemDeactivatedAfterEdit())
            Settings::Save();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("For a drifting stick: small movements are ignored, the rest is rescaled to keep full speed");

        const char* teams[] = {"Red Team", "Blue Team"};
        if (ImGui::Combo("Team", &ms.teamIndex[index], teams, IM_ARRAYSIZE(teams))) {
            Settings::ApplyPlayerTeam(index);
            Settings::Save();
        }

        const char* colorLabels[] = {"Primary Color", "Secondary Color", "Tertiary Color"};
        for (int c = 0; c < 3; ++c) {
            if (ColorCombo(colorLabels[c], &ms.playerColors[index].colors[c])) {
                Settings::ApplyPlayerColors(index);
                ReloadSettings();
                Settings::Save();
            }
        }

        ImGui::PopItemWidth();
    }

    // One game's armor for a player: its characters, Use Game Armor, the
    // ServiceTag and the armor pickers, which only offer that game's items.
    // Saved per game, and applied only while that game runs.
    static void ArmorGameContext(int index, CGameManager::Profile_t* p_profile, int game) {
        static int selected[4][Settings::ArmorGameCount] = {{-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}, {-1, -1, -1, -1}};
        int& chosen = selected[index][game];
        auto& characters = Settings::Characters();
        auto& armor = Settings::Armor(index, game);
        bool current = Settings::CurrentArmorGame() == game;
        bool changed = false;

        if (chosen >= (int)characters.size() || (chosen >= 0 && characters[chosen].game != game))
            chosen = -1;

        std::string name = Settings::CharacterName(p_profile->name);
        if (ImGui::Button("Save Character")) {
            chosen = Settings::SaveCharacter(name, game, armor, p_profile->name,
                                             &Settings::LookColors(index, Settings::GameOfArmorGame(game)));
            Settings::Save();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Saves this player's %s armor, service tag and name as \"%s\"",
                              Settings::ArmorGameName(game), name.c_str());

        ImGui::PushItemWidth(250);
        const char* preview = chosen >= 0 ? characters[chosen].name.c_str() : "-- Select Character --";
        if (ImGui::BeginCombo("Load Character", preview)) {
            for (int i = 0; i < (int)characters.size(); ++i) {
                if (characters[i].game != game) continue;
                ImGui::PushID(i);
                if (ImGui::Selectable(characters[i].name.c_str(), chosen == i)) {
                    chosen = i;
                    Settings::LoadCharacter(i, index, p_profile->name, (int)std::size(p_profile->name));
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();

        ImGui::SameLine();
        ImGui::BeginDisabled(chosen < 0);
        if (ImGui::Button("Delete##character")) {
            Settings::DeleteCharacter(chosen);
            Settings::Save();
            chosen = -1;
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!Settings::HasArmor(index, game));
        if (ImGui::Button("Use Game Armor")) {
            Settings::ClearArmor(index, game);
            Settings::Save();
            if (current)
                ReloadSettings();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Drops the %s armor picked here, going back to the armor from the game",
                              Settings::ArmorGameName(game));
        ImGui::EndDisabled();
        ImGui::Separator();

        // ServiceTag: up to four characters, none = the game's own
        int tag_index = Settings::ArmorServiceTagIndex();
        char tag[5] = {};
        for (int i = 0; i < 4; ++i)
            tag[i] = (char)armor[tag_index + i];
        if (ImGui::InputText("ServiceTag", tag, sizeof(tag))) {
            for (int i = 0; i < 4; ++i)
                armor[tag_index + i] = (unsigned char)tag[i];
            changed = true;
        }

        changed |= CUserProfile::ArmorImGuiContext((CUserProfile::eItemGame)Settings::ArmorItemGame(game),
            [&](int CUserProfile::* field) -> int* {
                int v = Settings::ArmorValueIndex(field);
                return v >= 0 ? &armor[v] : nullptr;
            });

        if (changed) {
            Settings::Save();
            if (current)
                ReloadSettings();
        }
    }

    // Edits the buttons of controller profile `profile` - a Reset to Defaults
    // button and a row per action - for every player using it, and saves.
    // `owner` tells apart the places it's shown, so a bind started in one
    // doesn't show up in another; `controller` is the pad a bind listens to.
    static void ProfileButtonsContext(const char* owner, int profile, int controller) {
        static std::string bind_owner;
        static int bind_profile = -1;
        static int bind_action = -1;
        static bool bind_armed = false; // the bind waits for every button to be let go first

        auto& profiles = Settings::Profiles();
        if (profile < 0 || profile >= (int)profiles.size())
            return;
        auto& buttons = profiles[profile].buttons;
        auto button_names = CGamepadMapping::ButtonNames();
        bool binding_here = bind_owner == owner && bind_profile == profile;
        bool changed = false;

        ImGui::PushID(owner);

        if (ImGui::Button("Reset to Defaults")) {
            Settings::ResetProfile(profile);
            if (binding_here) bind_action = -1;
            ReloadSettings();
            Settings::Save();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Puts \"%s\" back to its default buttons", profiles[profile].name.c_str());
        ImGui::Separator();

        if (binding_here && bind_action >= 0) {
            int pressed = CGamepadMapping::DetectPressedButton(controller);
            if (pressed < 0) {
                bind_armed = true;
            } else if (bind_armed) {
                Settings::ProfileButton(buttons, bind_action) = static_cast<CGamepadMapping::eButton>(pressed);
                bind_action = -1;
                changed = true;
            }
        }

        for (int a = 0; a < Settings::k_profileActionCount; ++a) {
            ImGui::PushID(a);
            auto& button = Settings::ProfileButton(buttons, a);
            if (binding_here && bind_action == a) {
                ImGui::Text("%s:", Settings::ProfileActionName(a));
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press button...");
                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                    bind_action = -1;
            } else {
                // None (-1) shows as the last entry
                int value = (button == CGamepadMapping::None) ? 16 : static_cast<int>(button);
                ImGui::PushItemWidth(150);
                if (ImGui::Combo(Settings::ProfileActionName(a), &value, button_names->data(), (int)button_names->size())) {
                    button = (value == 16) ? CGamepadMapping::None : static_cast<CGamepadMapping::eButton>(value);
                    changed = true;
                }
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button("Bind")) {
                    bind_owner = owner;
                    bind_profile = profile;
                    bind_action = a;
                    bind_armed = false;
                }
            }
            ImGui::PopID();
        }

        ImGui::PopID();

        if (changed) {
            Settings::ReapplyProfile(profile);
            ReloadSettings();
            Settings::Save();
        }
    }

    void ProfileContext(int index) {
        char buffer[1024];
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        auto p_profile = CGameManager::get_profile(index);
        auto& ms = Settings::State();
        const char* items[] = {"Controller 1", "Controller 2", "Controller 3", "Controller 4", "NONE"};

        if (p_profile == nullptr)
            return;

        ImGui::PushItemWidth(200);
        String::convert(buffer, p_profile->name, 1024);
        if (ImGui::InputText("Name", buffer, sizeof(buffer)))
            String::convert(p_profile->name, buffer, 1024);
        ImGui::PopItemWidth();

        ImGui::BeginDisabled(!index && p_setting->b_player0_use_km);

        int controller_index = p_profile->controller_index;

        // Check for controller binding completion
        if (s_binding_player == index) {
            int detected = DetectActiveController();
            if (detected >= 0) {
                p_profile->controller_index = detected;
                s_binding_player = -1;
            }
        }

        if (s_binding_player == index) {
            // Show binding prompt
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Press any button on controller...");
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                s_binding_player = -1;
            }
        } else {
            // Show dropdown and bind button
            ImGui::PushItemWidth(200);
            ImGui::Combo("Input", &p_profile->controller_index, items, IM_ARRAYSIZE(items));
            ImGui::PopItemWidth();
            ImGui::SameLine();
            sprintf(buffer, "Bind##ctrl%d", index);
            if (ImGui::Button(buffer) && s_binding_player < 0) {
                s_binding_player = index;
            }
        }

        if (p_profile->controller_index != controller_index) {
            ms.controllerIndex[index] = p_profile->controller_index;
            Settings::Save();
        }

        ImGui::EndDisabled();

        SharedPlayerContext(index);

        // "Copy Player 1's Look" only shares the look; controls stay each player's own
        bool look_shared = index && p_setting->b_use_player0_profile;

        if (ImGui::CollapsingHeader("Gamepad Mapping")) {
            ImGui::Indent();

            ImGui::PushItemWidth(200);
            if (ProfileCombo("Controller Profile##mapping", &ms.controllerProfile[index])) {
                Settings::ApplyPlayerMapping(index);
                ReloadSettings();
                Settings::Save();
            }
            ImGui::PopItemWidth();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Changing a button below changes this profile for every player using it");

            int profile = ms.controllerProfile[index];
            auto& profiles = Settings::Profiles();
            bool shared = false;
            for (int i = 0; i < 4; ++i) shared |= i != index && ms.controllerProfile[i] == profile;
            bool own = profile >= 0 && profile < (int)profiles.size() && profiles[profile].builtin < 0 && !shared;
            if (!own) {
                ImGui::TextWrapped(profile < 0 ? "Using the buttons set in MCC. Customize to give this player buttons of their own."
                                               : "This profile is a preset or shared with another player. Customize to give this player a copy of their own.");
                if (ImGui::Button("Customize My Buttons")) {
                    Settings::OwnProfile(index, ms);
                    ReloadSettings();
                    Settings::Save();
                }
            } else {
                // binds listen to this player's own controller
                int controller = (p_profile->controller_index >= 0 && p_profile->controller_index < 4) ? p_profile->controller_index : 0;
                char owner[16];
                snprintf(owner, sizeof(owner), "player%d", index);
                ProfileButtonsContext(owner, profile, controller);
                ImGui::TextDisabled("Other actions (Sprint, vehicles, Forge...) keep MCC's buttons");
            }
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Armor")) {
            ImGui::Indent();
            if (look_shared)
                ImGui::TextDisabled("\"Copy player 1's look\" is on: this player wears player 1's look");
            ImGui::BeginDisabled(look_shared);
            bool in_match = Settings::CurrentGame() >= 0;
            ImGui::BeginDisabled(!in_match);
            if (ImGui::Button("Copy Look From MCC")) {
                Settings::CaptureLook(index);
                Settings::Save();
                ReloadSettings();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Gives this player the armor, colors and service tag set in MCC right now, for the game being played.\n"
                                  "Change MCC's customization between missions/matches to give each player a different look.");
            ImGui::SameLine();
            if (ImGui::Button("Use MCC Look")) {
                Settings::ClearLook(index);
                Settings::Save();
                ReloadSettings();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drops this player's look (for the game being played, or all games when none is loaded)");
            for (int game = 0; game < Settings::ArmorGameCount; ++game) {
                ImGui::PushID(game);
                if (ImGui::CollapsingHeader(Settings::ArmorGameName(game))) {
                    ImGui::Indent();
                    ArmorGameContext(index, p_profile, game);
                    ImGui::Unindent();
                }
                ImGui::PopID();
            }
            ImGui::EndDisabled();
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader("Profile (read only)")) {
            ImGui::Indent();
            ImGui::TextWrapped("What the game last got for this player: the MCC profile with this player's settings above laid over it. "
                               "It is rebuilt every time the game asks, so change the settings above instead.");
            ImGui::BeginDisabled(true);
            p_profile->profile.ImGuiContext();
            ImGui::EndDisabled();
            ImGui::Unindent();
        }
    }

    // What each controller sends right now and who it belongs to, then what
    // each player's profile last gave the game: for checking a setup.
    static void ControllerTestContext() {
        auto& ms = Settings::State();
        DWORD real = AlphaRing::Input::RealConnectedMask();
        static const struct { WORD bit; const char* name; } k_buttons[] = {
            {XINPUT_GAMEPAD_A, "A"}, {XINPUT_GAMEPAD_B, "B"}, {XINPUT_GAMEPAD_X, "X"}, {XINPUT_GAMEPAD_Y, "Y"},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, "LB"}, {XINPUT_GAMEPAD_RIGHT_SHOULDER, "RB"},
            {XINPUT_GAMEPAD_LEFT_THUMB, "LS"}, {XINPUT_GAMEPAD_RIGHT_THUMB, "RS"},
            {XINPUT_GAMEPAD_START, "Start"}, {XINPUT_GAMEPAD_BACK, "Back"},
            {XINPUT_GAMEPAD_DPAD_UP, "Up"}, {XINPUT_GAMEPAD_DPAD_DOWN, "Down"},
            {XINPUT_GAMEPAD_DPAD_LEFT, "Left"}, {XINPUT_GAMEPAD_DPAD_RIGHT, "Right"},
        };

        if (ImGui::BeginTable("controllers", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Slot");
            ImGui::TableSetupColumn("Controller");
            ImGui::TableSetupColumn("Player");
            ImGui::TableSetupColumn("Input now");
            ImGui::TableHeadersRow();
            for (int slot = 0; slot < 4; ++slot) {
                XINPUT_STATE state;
                bool connected = AlphaRing::Input::GetXInputGetState(slot, &state);
                std::string ps = AlphaRing::Input::PlayStation::SlotName(slot);
                std::string players;
                for (int i = 0; i < ms.playerCount && i < 4; ++i) {
                    if (i == 0 && ms.useKM) continue;
                    if (ms.controllerIndex[i] == slot)
                        players += (players.empty() ? "" : ", ") + std::string("Player ") + std::to_string(i + 1);
                }
                std::string pressed;
                for (auto& b : k_buttons)
                    if (state.Gamepad.wButtons & b.bit) pressed += std::string(b.name) + " ";

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%d", slot + 1);
                ImGui::TableNextColumn();
                if (!ps.empty())
                    ImGui::TextUnformatted(ps.c_str());
                else if (real & (1u << slot))
                    ImGui::TextUnformatted("XInput (Xbox, or translated by Steam/DS4Windows)");
                else
                    ImGui::TextDisabled(connected ? "connected" : "none");
                ImGui::TableNextColumn();
                if (players.empty()) ImGui::TextDisabled("-");
                else ImGui::TextUnformatted(players.c_str());
                ImGui::TableNextColumn();
                if (connected) {
                    const auto& g = state.Gamepad;
                    ImGui::Text("L %+.2f %+.2f  R %+.2f %+.2f  LT %3d RT %3d  %s",
                                g.sThumbLX / 32767.0f, g.sThumbLY / 32767.0f, g.sThumbRX / 32767.0f, g.sThumbRY / 32767.0f,
                                g.bLeftTrigger, g.bRightTrigger, pressed.c_str());
                } else {
                    ImGui::TextDisabled("-");
                }
            }
            ImGui::EndTable();
        }
        if (ms.useKM)
            ImGui::TextDisabled("Player 1 is on keyboard/mouse");

        ImGui::Spacing();
        ImGui::TextUnformatted("Last given to the game (it asks when a mission/match loads and after settings change):");
        auto& profiles = Settings::Profiles();
        for (int i = 0; i < ms.playerCount && i < 4; ++i) {
            auto profile = CGameManager::get_profile(i);
            if (!profile) continue;
            const auto& p = profile->profile;
            int layout = ms.controllerProfile[i];
            const char* layoutName = (layout >= 0 && layout < (int)profiles.size()) ? profiles[layout].name.c_str() : "MCC Settings";
            ImGui::BulletText("Player %d: sensitivity %d/%d, look %s, flying %s, vibration %s, colors %d/%d/%d, buttons \"%s\", deadzone %d%%",
                              i + 1, p.HorizontalLookSensitivity, p.VerticalLookSensitivity,
                              p.LookControlsInverted ? "inverted" : "normal", p.AircraftControlsInverted ? "inverted" : "normal",
                              p.VibrationDisabled ? "off" : "on", p.PlayerModelPrimaryColorIndex, p.PlayerModelSecondaryColorIndex,
                              p.PlayerModelTertiaryColorIndex, layoutName, ms.deadzone[i]);
        }
    }

    static bool s_confirm_reset = false;

    static void ResetPopup() {
        if (s_confirm_reset) {
            ImGui::OpenPopup("Reset Alpha Ring settings?");
            s_confirm_reset = false;
        }
        if (ImGui::BeginPopupModal("Reset Alpha Ring settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("Deletes the mod's settings files next to the DLL:");
            ImGui::BulletText("alpha_ring_menu.bin - players, controls, sensitivity, looks, characters");
            ImGui::BulletText("alpha_ring_controllers.cfg - which PlayStation controller is which");
            ImGui::BulletText("alpha_ring_patches.cfg - Dev Tools patch toggles (after restarting MCC)");
            ImGui::BulletText("alpha_ring_splitscreen.cfg - Reach split-screen layout (after restarting MCC)");
            ImGui::TextUnformatted("MCC's own settings and profiles are not touched.");
            ImGui::Separator();
            if (ImGui::Button("Reset", ImVec2(120, 0))) {
                if (Settings::ResetAll())
                    AlphaRing::Toast::Show("Alpha Ring settings reset to the defaults");
                else
                    AlphaRing::Toast::Show("Leave the mission/match first");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void RealContext() {
        char buffer[10];
        auto p_setting = AlphaRing::Global::MCC::Splitscreen();
        auto& ms = Settings::State();
        ResetPopup();

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("Options")) {
                if (ImGui::MenuItem("Copy player 1's look", nullptr, &p_setting->b_use_player0_profile)) {
                    ms.usePlayer0Profile = p_setting->b_use_player0_profile;
                    Settings::Save();
                    ReloadSettings();
                }
                if (ImGui::MenuItem("Anyone can pause", nullptr, &ms.anyoneCanPause))
                    Settings::Save();
                if (ImGui::MenuItem("PlayStation controllers (USB/Bluetooth)", nullptr, &ms.nativePlayStation))
                    Settings::Save();
                if (ImGui::MenuItem("Halo 2: Anniversary graphics with 3-4 players (experimental)", nullptr, &ms.anniversaryQuad)) {
                    Settings::ApplySplitscreen(false);
                    Settings::Save();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Halo 2's Anniversary renderer draws two views; this mode draws all four, two per frame in turn\n"
                                      "(each view at half the frame rate). Takes effect as a mission starts. From kirklandsig/AlphaRing.");
                if (ImGui::MenuItem("Halo 2: two-view black-screen fix", nullptr, &ms.halo2TwoViewFix)) {
                    Settings::ApplySplitscreen(false);
                    Settings::Save();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("With two players, the mod draws each Anniversary view into its half itself.\n"
                                      "Only if Halo 2 Anniversary split screen shows a black screen (seen under Proton).");
                if (ImGui::MenuItem("Halo CE/2: Classic graphics with 3-4 players", nullptr, &ms.classicGraphics3P)) {
                    Settings::ApplySplitscreen(false);
                    Settings::Save();
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Anniversary graphics in Halo CE and Halo 2 can only draw two views, so with 3-4 players a mission\n"
                                      "starts in Classic graphics, where everyone gets a view. Back/Tab still switches mid-mission.");
                ImGui::Separator();
                ImGui::BeginDisabled(MCC::IsInGame());
                if (ImGui::MenuItem("Reset all Alpha Ring settings..."))
                    s_confirm_reset = true;
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(MCC::IsInGame() ? "Leave the mission/match first"
                                                      : "Deletes every Alpha Ring settings file and starts over with the defaults");
                if (ImGui::MenuItem("Enable K/M for player1", nullptr, &p_setting->b_player0_use_km)) {
                    ms.useKM = p_setting->b_player0_use_km;
                    Settings::Save();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Players: [-] n [+]
        int player_count = ms.playerCount;
        float button_size = ImGui::GetFrameHeight();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Players");
        ImGui::SameLine();
        ImGui::BeginDisabled(player_count <= 1);
        if (ImGui::Button("-", ImVec2(button_size, button_size)))
            --player_count;
        ImGui::EndDisabled();
        ImGui::SameLine();
        char count_text[4];
        snprintf(count_text, sizeof(count_text), "%d", ms.playerCount);
        // centered in a box as wide as a button
        float text_width = ImGui::CalcTextSize(count_text).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (button_size - text_width) * 0.5f);
        ImGui::TextUnformatted(count_text);
        ImGui::SameLine(0.0f, ImGui::GetStyle().ItemSpacing.x + (button_size - text_width) * 0.5f);
        ImGui::BeginDisabled(player_count >= 4);
        if (ImGui::Button("+", ImVec2(button_size, button_size)))
            ++player_count;
        ImGui::EndDisabled();
        if (player_count != ms.playerCount) {
            ms.playerCount = player_count;
            Settings::ApplySplitscreen(false);
            Settings::Save();
        }

        {
            auto status = Halo2::Entry::Anniversary::Status();
            ImGui::TextDisabled("Halo 2 Anniversary 3-4 players: %s", status.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("hooked: the mode's hooks are in halo2.dll - active: this mission has the mode - quad: it is drawing four views right now");
        }

        if (ImGui::CollapsingHeader("Controller Test")) {
            ImGui::Indent();
            ControllerTestContext();
            ImGui::Unindent();
        }

        if (ImGui::BeginTabBar("Players")) {
            for (int i = 0; i < p_setting->player_count; ++i) {
                sprintf(buffer, "Player %d", i + 1);
                if (ImGui::BeginTabItem(buffer)) {
                    ProfileContext(i);
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
    }
}
