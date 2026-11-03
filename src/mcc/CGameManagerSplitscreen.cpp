#include "CGameManager.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>

#include "common.h"

#include "./mcc.h"
#include "global/Global.h"
#include "input/Input.h"
#include "mcc/settings/MenuSettings.h"
#include "log/MatchLog.h"
#include "render/imgui/Toast.h"

using AlphaRing::MatchLog::Describe;
using AlphaRing::MatchLog::FirstThisMatch;

// A match is being set up or running: the profiles and button layouts the
// game asks for now are for play, not for MCC's menus (whose settings screens
// must see and edit the MCC profile itself).
// From game_setup until MCC reports the match running (loading can take a
// while), at most two minutes, so a missed exit can't keep MCC's menus off
// the MCC profile.
static std::atomic<bool> s_match_starting{false};
static std::atomic<ULONGLONG> s_match_started_at{0};

bool CGameManager::in_match() {
    if (MCC::IsInGame()) {
        s_match_starting = false;
        return true;
    }
    return s_match_starting.load() && GetTickCount64() - s_match_started_at.load() < 120000;
}

void CGameManager::set_match_active(bool active) {
    s_match_started_at = GetTickCount64();
    s_match_starting = active;
}

void CGameManager::set_vibration(CGameManager *self, DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
    CInputDevice* p_device;
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();

    if (!p_setting->b_override) {
        ppOriginal.set_vibration(self, dwUserIndex, pVibration);
        return;
    }

    if (dwUserIndex >= p_setting->player_count)
        return;

    if (pVibration == nullptr || (p_device = get_controller(dwUserIndex)) == nullptr)
        return;

    XINPUT_VIBRATION off{};
    AlphaRing::Input::SetState(p_device->input_user,
                               AlphaRing::MenuSettings::VibrationOff((int)dwUserIndex) ? &off : pVibration);
}

static bool get_xbox_user_id_inner(CGameManager *self, __int64 *pId, wchar_t *pName, int size, int index,
                                   const char** path) {
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    auto p_profile = CGameManager::get_profile(index);

    if (!p_setting->b_override || !index) {
        *path = "original";
        return CGameManager::ppOriginal.get_xbox_user_id(self, pId, pName, size, index);
    }

    if (index >= p_setting->player_count) {
        *path = "beyond player_count";
        return false;
    }

    *path = "alpha ring";
    if (pId)
        *pId = p_profile->id;

    if (pName)
        String::wstrcpy(pName, p_profile->name, size >> 1);

    return true;
}

bool CGameManager::get_xbox_user_id(CGameManager *self, __int64 *pId, wchar_t *pName, int size, int index) {
    const char* path = "";
    bool result = get_xbox_user_id_inner(self, pId, pName, size, index, &path);

    // once per match per slot, and whenever a slot's answer changes
    static std::atomic<long> s_seen[8];
    static std::atomic<int> s_last[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (index >= 0 && index < 8) {
        bool first = FirstThisMatch(s_seen[index]);
        bool changed = s_last[index].exchange(result ? 1 : 0) != (result ? 1 : 0);
        if (first || changed)
            MATCH_LOG("get_xbox_user_id index={} -> {} id={:#x} via {} (caller {})", index, result,
                      (result && pId) ? (unsigned long long)*pId : 0ull, path, Describe(_ReturnAddress()));
    } else {
        MATCH_LOG("get_xbox_user_id index={} (out of range) -> {} via {}", index, result, path);
    }
    return result;
}

// ---------------------------------------------------------------- shared pause

// Start held on the controller of any player but player 1, for "Anyone Can
// Pause": the game only takes Start (its pause menu) from player 1.
static bool others_hold_start(int player0_pad) {
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    if (!p_setting->b_override || !AlphaRing::MenuSettings::AnyoneCanPause())
        return false;
    for (int i = 1; i < p_setting->player_count && i < 4; ++i) {
        auto profile = CGameManager::get_profile(i);
        if (!profile) continue;
        int pad = profile->controller_index;
        if (pad < 0 || pad >= 4 || pad == player0_pad) continue;
        if (AlphaRing::Input::RecentButtons((DWORD)pad) & XINPUT_GAMEPAD_START)
            return true;
    }
    return false;
}

// Applied to MCC's own XInputGetState reads (its pause menu and shell).
void CGameManager::filter_pad(DWORD slot, XINPUT_STATE* state) {
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    if (!p_setting->b_override || p_setting->b_player0_use_km || !in_match())
        return;
    auto profile = get_profile(0);
    if (!profile || profile->controller_index != (int)slot)
        return;
    if (others_hold_start((int)slot)) {
        state->Gamepad.wButtons |= XINPUT_GAMEPAD_START;
        state->dwPacketNumber ^= 0x40000000; // a new packet number, as the buttons differ from the pad's
    }
}

// Player 1 on keyboard/mouse pauses with Escape.
static void press_escape() {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_ESCAPE;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// An extra radial deadzone, rescaled so the stick still reaches full speed:
// for a drifting stick. `percent` of full deflection.
static void apply_deadzone(SHORT& x, SHORT& y, int percent) {
    if (percent <= 0) return;
    float dz = percent / 100.0f;
    float fx = x / 32767.0f, fy = y / 32767.0f;
    float magnitude = sqrtf(fx * fx + fy * fy);
    if (magnitude <= dz) {
        x = y = 0;
        return;
    }
    float scale = (fminf(magnitude, 1.0f) - dz) / (1.0f - dz) / magnitude;
    x = (SHORT)fmaxf(-32768.0f, fminf(32767.0f, fx * scale * 32767.0f));
    y = (SHORT)fmaxf(-32768.0f, fminf(32767.0f, fy * scale * 32767.0f));
}

// Tells the players when a controller in the match drops out or comes back.
static void track_connection(int index, bool connected) {
    static int s_state[4] = {-1, -1, -1, -1}; // -1 unknown, 0 gone, 1 there
    static std::atomic<long> s_seen[4];
    if (index < 0 || index >= 4) return;
    if (AlphaRing::MatchLog::FirstThisMatch(s_seen[index]))
        s_state[index] = -1; // a new match: no notice for how it starts
    int now = connected ? 1 : 0;
    if (s_state[index] != -1 && s_state[index] != now)
        AlphaRing::Toast::Show(connected ? "Player %d's controller is back" : "Player %d's controller disconnected", index + 1);
    s_state[index] = now;
}

bool CGameManager::get_key_state(CGameManager *self, DWORD index, input_data_t *p_input) {
    bool result;
    LARGE_INTEGER qpc;
    float delta_time = 0;
    CInputDevice* p_device;

    auto p_profile = AlphaRing::Global::MCC::Splitscreen();
    auto device_manager = DeviceManager();
    auto p_global = AlphaRing::Global::Global();

    memset(p_input, 0, sizeof(input_data_t));

    static std::atomic<long> s_seen[8];
    if (index < 8 && FirstThisMatch(s_seen[index]))
        MATCH_LOG("get_key_state index={} override={} player_count={} menu={} (caller {})", index,
                  p_profile->b_override, p_profile->player_count, p_global->show_imgui, Describe(_ReturnAddress()));

    if (p_global->show_imgui) {
        if (p_global->pause_game_on_menu_shown)
            return false;
        else if (p_global->disable_input_on_menu_shown)
            return true;
    }

    if (!p_profile->b_override)
        return ppOriginal.get_key_state(self, index, p_input);

    if (index >= p_profile->player_count)
        return false;

    static std::atomic<long> s_device_seen[8];
    bool log_device = index < 8 && FirstThisMatch(s_device_seen[index]);

    if (p_profile->b_player0_use_km && !index) {
        if (log_device)
            MATCH_LOG("input player={} reads keyboard/mouse", index);
        p_device = device_manager->p_input_device[4];
        device_manager->table->update_state(device_manager, 0, 0, false);
        QueryPerformanceCounter(&qpc);
        auto v1 = qpc.QuadPart - device_manager->qpc.QuadPart;
        auto v2 = device_manager->qpc.QuadPart - qpc.QuadPart;
        delta_time = fminf(fmaxf(MCC::DeltaTime(v1 >= v2 ? v2 : v1) * 1000.0,0.1), 1000.0);
        device_manager->qpc = qpc;
    } else {
        if ((p_device = get_controller(index)) == nullptr) {
            if (log_device)
                MATCH_LOG("input player={} has no controller (controller_index={})", index,
                          get_profile(index) ? get_profile(index)->controller_index : -1);
            return true;
        }

        bool connected = AlphaRing::Input::GetXInputGetState(p_device->input_user, &p_device->state);
        if (log_device)
            MATCH_LOG("input player={} reads controller_index={} input_user={} connected={}", index,
                      get_profile(index)->controller_index, p_device->input_user, connected);
        track_connection((int)index, connected);

        // first button press per player per match: shows a pad's input reaches that player
        static std::atomic<long> s_press_seen[8];
        if (index < 8 && p_device->state.Gamepad.wButtons != 0 && FirstThisMatch(s_press_seen[index]))
            MATCH_LOG("input player={} first press buttons={:#06x} (input_user={})", index,
                      p_device->state.Gamepad.wButtons, p_device->input_user);

        if (int deadzone = AlphaRing::MenuSettings::StickDeadzone((int)index)) {
            apply_deadzone(p_device->state.Gamepad.sThumbLX, p_device->state.Gamepad.sThumbLY, deadzone);
            apply_deadzone(p_device->state.Gamepad.sThumbRX, p_device->state.Gamepad.sThumbRY, deadzone);
        }

        // the Xbox menu is being driven with the controllers
        if (AlphaRing::Input::XboxMenuOpen())
            memset(&p_device->state.Gamepad, 0, sizeof(p_device->state.Gamepad));

        // Anyone Can Pause: another player's Start reaches player 1, or
        // Escape when player 1 is on keyboard/mouse.
        static bool s_start_was_down[4];
        bool start = (p_device->state.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
        bool menu_open = AlphaRing::Input::XboxMenuOpen();
        if (menu_open) {
            // nothing reaches the game, and no one pauses it behind the menu
        } else if (index == 0 && others_hold_start(p_device->input_user)) {
            p_device->state.Gamepad.wButtons |= XINPUT_GAMEPAD_START;
            p_device->state.dwPacketNumber ^= 0x40000000;
        } else if (index > 0 && index < 4 && start && !s_start_was_down[index] && p_profile->b_player0_use_km &&
                   AlphaRing::MenuSettings::AnyoneCanPause()) {
            press_escape();
        }
        if (index < 4) s_start_was_down[index] = start;
    }

    // set_state reads the device's state; should it ask XInput itself, this
    // thread is handed the same state for that slot.
    if (p_device != device_manager->p_input_device[4])
        AlphaRing::Input::SetThreadState(p_device->input_user, &p_device->state);
    result = p_device
            ->p_method_table
            ->set_state(p_device, delta_time, p_input);
    AlphaRing::Input::SetThreadState(0, nullptr);

    p_device
            ->p_method_table
            ->check(p_device);

    return result;
}

// The fields of the MCC profile a splitscreen player takes from it live: the
// look (armor, colors, service tag, weapon skins) and the controls. MCC puts
// the look in after game_setup (Reach, among others), so the snapshot taken
// there gave everyone the default look. Everything else (FOV, aspect ratio,
// HUD...) stays as in that snapshot, as it always was: with the live display
// settings, Halo 2 Anniversary drew each view at a quarter of the screen.
static void take_live_fields(const CUserProfile& live, CUserProfile& p) {
    auto copy = [&](size_t from, size_t to) {
        memcpy(reinterpret_cast<char*>(&p) + from, reinterpret_cast<const char*>(&live) + from, to - from);
    };
    auto field = [&](size_t offset, size_t size) { copy(offset, offset + size); };
#define LIVE(name) field(offsetof(CUserProfile, name), sizeof(CUserProfile::name))
    // controls
    LIVE(LookControlsInverted);
    LIVE(MouseLookControlsInverted);
    LIVE(VibrationDisabled);
    LIVE(ImpulseTriggersDisabled);
    LIVE(AircraftControlsInverted);
    LIVE(MouseAircraftControlsInverted);
    LIVE(AutoCenterEnabled);
    LIVE(CrouchLockEnabled);
    LIVE(MKCrouchLockEnabled);
    LIVE(ClenchProtectionEnabled);
    LIVE(HoldToZoom);
    copy(offsetof(CUserProfile, VerticalLookSensitivity), offsetof(CUserProfile, FlyingCameraTurnSensitivity)); // sensitivities, dead zones, presets
    copy(offsetof(CUserProfile, SwapTriggersAndBumpers), offsetof(CUserProfile, ColorCorrection));              // trigger/bumper swap, aim, dual wield, vehicles
    copy(offsetof(CUserProfile, MouseSensitivity), offsetof(CUserProfile, PixelPerfectHudScale));              // mouse (not the HUD scale between)
    copy(offsetof(CUserProfile, MouseAccelerationMinRate), offsetof(CUserProfile, MasterVolume));              // mouse acceleration, key bindings
    // look
    LIVE(PlayerModelPrimaryColorIndex);
    LIVE(PlayerModelSecondaryColorIndex);
    LIVE(PlayerModelTertiaryColorIndex);
    LIVE(UseEliteModel);
    LIVE(PlayerModelPermutation);
    copy(offsetof(CUserProfile, HelmetIndex), offsetof(CUserProfile, ServiceTag) + sizeof(CUserProfile::ServiceTag)); // armor, colors, poses, skins, tag
#undef LIVE
}

// The profile the game gets for a player: in split screen, the snapshot taken
// at the start of the mission/match with the live look and controls of the MCC
// profile (player 1's, the only one MCC has), then the player's own controls
// and look from the menus laid over it. Built on every call, for every game.
// The MCC profile itself is never written.
CUserProfile* CGameManager::get_player_profile(CGameManager *self, __int64 xid)  {
    auto index = get_index(xid);
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    bool split = p_setting->b_override;

    static std::atomic<long> s_seen[4];
    if (FirstThisMatch(s_seen[index]))
        MATCH_LOG("get_player_profile xid={:#x} -> index {} override={} (caller {})", (unsigned long long)xid, index,
                  split, Describe(_ReturnAddress()));

    if (!split && index)
        return ppOriginal.get_player_profile(self, xid);
    if (split && !p_setting->b_override_profile)
        return ppOriginal.get_player_profile(self, get_xuid(0));

    auto* base = ppOriginal.get_player_profile(self, index ? get_xuid(0) : xid);
    auto* dest = get_profile(index);
    if (!base || !dest)
        return (split && dest) ? &dest->profile : base;
    // MCC's own menus get the MCC profile (player 1 alone, or out of a match)
    if (!index && !in_match())
        return base;

    auto* gg = GameGlobal();
    int game = gg ? (int)gg->current_game : -1;
    // a lone player gets the live profile, as MCC gives it
    const CUserProfile* start = base;
    CUserProfile merged;
    if (split) {
        // once per match: where the live MCC profile differs from the
        // mission-start snapshot (byte ranges, offsets as in CUserProfile.h)
        static std::atomic<long> s_diff_seen;
        if (FirstThisMatch(s_diff_seen)) {
            const auto* a = reinterpret_cast<const unsigned char*>(&dest->settled);
            const auto* b = reinterpret_cast<const unsigned char*>(base);
            std::string ranges;
            int ranges_count = 0;
            for (size_t i = 0; i < sizeof(CUserProfile) && ranges_count < 64;) {
                if (a[i] == b[i]) { ++i; continue; }
                size_t start = i;
                while (i < sizeof(CUserProfile) && a[i] != b[i]) ++i;
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%s0x%zX-0x%zX", ranges.empty() ? "" : " ", start, i - 1);
                ranges += buffer;
                ++ranges_count;
            }
            LOG_INFO("profile: live MCC profile differs from the match-start snapshot at: {}",
                      ranges.empty() ? std::string("nothing") : ranges);
        }
        memcpy(&merged, &dest->settled, sizeof(CUserProfile));
        take_live_fields(*base, merged);
        start = &merged;
    }
    CUserProfile composed;
    bool changed = AlphaRing::MenuSettings::ComposeProfile(index, *start, composed, game);
    if (!split && !changed)
        return base;
    // only bytes that differ change, so a reader never sees the copy half-built
    if (memcmp(&dest->profile, &composed, sizeof(CUserProfile)) != 0)
        memcpy(&dest->profile, &composed, sizeof(CUserProfile));
    return &dest->profile;
}

// The button layout: MCC's (player 1's, as set in MCC's settings), with the
// player's controller profile laid over a copy of it, or MCC's as it is for
// a player on "MCC Settings".
CGamepadMapping* CGameManager::retrive_gamepad_mapping(CGameManager *self, __int64 xid) {
    auto index = get_index(xid);
    auto p_setting = AlphaRing::Global::MCC::Splitscreen();
    bool split = p_setting->b_override;

    static std::atomic<long> s_seen[4];
    if (FirstThisMatch(s_seen[index]))
        MATCH_LOG("retrive_gamepad_mapping xid={:#x} -> index {} (caller {})", (unsigned long long)xid, index,
                  Describe(_ReturnAddress()));

    if (!split && index)
        return ppOriginal.retrive_gamepad_mapping(self, xid);

    auto* base = ppOriginal.retrive_gamepad_mapping(self, index ? get_xuid(0) : xid);
    auto* dest = get_profile(index);
    if (!base || !dest)
        return (split && dest) ? &dest->mapping : base;
    // MCC's controller settings screen edits the real layout
    if (!index && !in_match())
        return base;

    CGamepadMapping composed;
    bool changed = AlphaRing::MenuSettings::ComposeMapping(index, *base, composed);
    if (!changed && !index)
        return base;
    if (memcmp(&dest->mapping, &composed, sizeof(CGamepadMapping)) != 0)
        memcpy(&dest->mapping, &composed, sizeof(CGamepadMapping));
    return &dest->mapping;
}
