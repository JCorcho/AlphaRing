#include "Input.h"
#include "MenuConfig.h"
#include "PlayStation.h"

#include "common.h"
#include "MinHook.h"

#include "imgui.h"
#include "global/Global.h"
#include "mcc/CGamepadMapping.h"
#include "render/imgui/ImGui.h"
#include "render/imgui/game/xbox/CXboxContext.h"

#include <atomic>

static HMODULE hModule;
static DWORD (WINAPI* g_pXInputGetState)(_In_ DWORD dwUserIndex, _Out_ XINPUT_STATE* pState) WIN_NOEXCEPT;
static DWORD (WINAPI* g_pXInputSetState)(_In_ DWORD dwUserIndex, _In_ XINPUT_VIBRATION* pVibration) WIN_NOEXCEPT;
static DWORD (WINAPI* g_pXInputGetCapabilities)(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities);
static DWORD (WINAPI* g_pXInputGetStateEx)(DWORD dwUserIndex, XINPUT_STATE* pState); // ordinal 100: adds the Guide button

// XInputGetCapabilitiesEx (xinput1_4 ordinal 108): the capabilities plus the
// device's USB ids, which games use to tell controllers apart.
struct XINPUT_CAPABILITIES_EX_T {
    XINPUT_CAPABILITIES Capabilities;
    WORD VendorId;
    WORD ProductId;
    WORD ProductVersion;
    WORD Unknown1;
    DWORD Unknown2;
};
static DWORD (WINAPI* g_pXInputGetCapabilitiesEx)(DWORD unknown, DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES_EX_T* pCapabilities);
static DWORD (WINAPI* g_pXInputGetBatteryInformation)(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation);

// SetThreadState: the player whose input set_state is building on this thread.
static thread_local const XINPUT_STATE* t_override_state = nullptr;
static thread_local DWORD t_override_slot = 0;
static std::atomic<AlphaRing::Input::PadFilter> g_pad_filter{nullptr};

// The latest successful read of each slot, for RecentButtons.
static std::atomic<WORD> g_last_buttons[4];
static std::atomic<ULONGLONG> g_last_read[4];

static void RememberRead(DWORD slot, const XINPUT_STATE* state) {
    if (slot >= 4) return;
    g_last_buttons[slot] = state ? state->Gamepad.wButtons : 0;
    g_last_read[slot] = GetTickCount64();
}

// MCC reading a controller itself (its menus, its pause menu, a lone player).
static DWORD ReadForGame(DWORD dwUserIndex, XINPUT_STATE* pState, bool guide) {
    if (t_override_state && t_override_slot == dwUserIndex) {
        *pState = *t_override_state;
        return ERROR_SUCCESS;
    }
    DWORD result = (guide && g_pXInputGetStateEx) ? g_pXInputGetStateEx(dwUserIndex, pState)
                                                  : g_pXInputGetState(dwUserIndex, pState);
    if (result != ERROR_SUCCESS && AlphaRing::Input::PlayStation::GetState(dwUserIndex, pState, guide))
        result = ERROR_SUCCESS;
    if (result != ERROR_SUCCESS)
        return result;
    if (g_pXboxContext && g_pXboxContext->isOpen()) {
        ZeroMemory(&pState->Gamepad, sizeof(pState->Gamepad));
    } else if (auto filter = g_pad_filter.load()) {
        filter(dwUserIndex, pState);
    }
    return result;
}

static DWORD WINAPI XInputGetStateDetour(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (!pState) return g_pXInputGetState(dwUserIndex, pState);
    return ReadForGame(dwUserIndex, pState, false);
}

static DWORD WINAPI XInputGetStateExDetour(DWORD dwUserIndex, XINPUT_STATE* pState) {
    if (!pState) return g_pXInputGetStateEx(dwUserIndex, pState);
    return ReadForGame(dwUserIndex, pState, true);
}

static DWORD WINAPI XInputSetStateDetour(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration) {
    DWORD result = g_pXInputSetState(dwUserIndex, pVibration);
    if (result != ERROR_SUCCESS && AlphaRing::Input::PlayStation::SetVibration(dwUserIndex, pVibration))
        result = ERROR_SUCCESS;
    return result;
}

static DWORD WINAPI XInputGetCapabilitiesDetour(DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES* pCapabilities) {
    DWORD result = g_pXInputGetCapabilities(dwUserIndex, dwFlags, pCapabilities);
    if (result == ERROR_SUCCESS || !pCapabilities || dwUserIndex >= 4 ||
        !(AlphaRing::Input::PlayStation::ConnectedMask() & (1u << dwUserIndex)))
        return result;
    // a standard wired gamepad, as XInput reports one
    ZeroMemory(pCapabilities, sizeof(*pCapabilities));
    pCapabilities->Type = XINPUT_DEVTYPE_GAMEPAD;
    pCapabilities->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    pCapabilities->Gamepad.wButtons = 0xF3FF;
    pCapabilities->Gamepad.bLeftTrigger = 0xFF;
    pCapabilities->Gamepad.bRightTrigger = 0xFF;
    pCapabilities->Gamepad.sThumbLX = (SHORT)0xFFC0;
    pCapabilities->Gamepad.sThumbLY = (SHORT)0xFFC0;
    pCapabilities->Gamepad.sThumbRX = (SHORT)0xFFC0;
    pCapabilities->Gamepad.sThumbRY = (SHORT)0xFFC0;
    pCapabilities->Vibration.wLeftMotorSpeed = 0xFF;
    pCapabilities->Vibration.wRightMotorSpeed = 0xFF;
    return ERROR_SUCCESS;
}

// A DualShock 4 / DualSense in the slot (no XInput controller there).
static bool PlayStationOnly(DWORD dwUserIndex) {
    return dwUserIndex < 4 && (AlphaRing::Input::PlayStation::ConnectedMask() & (1u << dwUserIndex));
}

static DWORD WINAPI XInputGetCapabilitiesExDetour(DWORD unknown, DWORD dwUserIndex, DWORD dwFlags, XINPUT_CAPABILITIES_EX_T* pCapabilities) {
    DWORD result = g_pXInputGetCapabilitiesEx(unknown, dwUserIndex, dwFlags, pCapabilities);
    if (result == ERROR_SUCCESS || !pCapabilities || !PlayStationOnly(dwUserIndex))
        return result;
    ZeroMemory(pCapabilities, sizeof(*pCapabilities));
    XInputGetCapabilitiesDetour(dwUserIndex, dwFlags, &pCapabilities->Capabilities);
    pCapabilities->VendorId = 0x045E;  // a wired Xbox 360 controller, the standard XInput device
    pCapabilities->ProductId = 0x028E;
    pCapabilities->ProductVersion = 0x0114;
    return ERROR_SUCCESS;
}

static DWORD WINAPI XInputGetBatteryInformationDetour(DWORD dwUserIndex, BYTE devType, XINPUT_BATTERY_INFORMATION* pBatteryInformation) {
    DWORD result = g_pXInputGetBatteryInformation(dwUserIndex, devType, pBatteryInformation);
    if (result == ERROR_SUCCESS || !pBatteryInformation || !PlayStationOnly(dwUserIndex))
        return result;
    pBatteryInformation->BatteryType = devType == BATTERY_DEVTYPE_GAMEPAD ? BATTERY_TYPE_WIRED : BATTERY_TYPE_DISCONNECTED;
    pBatteryInformation->BatteryLevel = BATTERY_LEVEL_FULL;
    return ERROR_SUCCESS;
}

template <typename T>
static void HookExport(LPVOID target, LPVOID detour, T** original, const char* name) {
    if (!target) return;
    if (MH_CreateHook(target, detour, reinterpret_cast<LPVOID*>(original)) == MH_OK && MH_EnableHook(target) == MH_OK)
        LOG_INFO("Input: hooked {}", name);
    else
        LOG_WARNING("Input: couldn't hook {}", name);
}

namespace AlphaRing::Input {
    bool Init() {
        if ((hModule = GetModuleHandleA("XINPUT1_3.dll")) ||
            (hModule = GetModuleHandleA("XINPUT1_4.dll")) ||
            (hModule = GetModuleHandleA("XINPUT9_1_0.dll"))) {
            g_pXInputGetState = (decltype(g_pXInputGetState))GetProcAddress(hModule, "XInputGetState");
            g_pXInputSetState = (decltype(g_pXInputSetState))GetProcAddress(hModule, "XInputSetState");
        }

        assertm(hModule != nullptr, "failed to find xinput module");

        g_pXInputGetCapabilities = (decltype(g_pXInputGetCapabilities))GetProcAddress(hModule, "XInputGetCapabilities");
        g_pXInputGetStateEx = (decltype(g_pXInputGetStateEx))GetProcAddress(hModule, (LPCSTR)100);
        g_pXInputGetCapabilitiesEx = (decltype(g_pXInputGetCapabilitiesEx))GetProcAddress(hModule, (LPCSTR)108);
        g_pXInputGetBatteryInformation = (decltype(g_pXInputGetBatteryInformation))GetProcAddress(hModule, "XInputGetBatteryInformation");

        MH_STATUS mhStatus = MH_Initialize();
        if (mhStatus == MH_OK || mhStatus == MH_ERROR_ALREADY_INITIALIZED) {
            HookExport((LPVOID)g_pXInputGetState, (LPVOID)&XInputGetStateDetour, &g_pXInputGetState, "XInputGetState");
            HookExport((LPVOID)g_pXInputSetState, (LPVOID)&XInputSetStateDetour, &g_pXInputSetState, "XInputSetState");
            HookExport((LPVOID)g_pXInputGetCapabilities, (LPVOID)&XInputGetCapabilitiesDetour, &g_pXInputGetCapabilities, "XInputGetCapabilities");
            HookExport((LPVOID)g_pXInputGetStateEx, (LPVOID)&XInputGetStateExDetour, &g_pXInputGetStateEx, "XInputGetStateEx");
            HookExport((LPVOID)g_pXInputGetCapabilitiesEx, (LPVOID)&XInputGetCapabilitiesExDetour, &g_pXInputGetCapabilitiesEx, "XInputGetCapabilitiesEx");
            HookExport((LPVOID)g_pXInputGetBatteryInformation, (LPVOID)&XInputGetBatteryInformationDetour, &g_pXInputGetBatteryInformation, "XInputGetBatteryInformation");
        }

        g_menuConfig = MenuConfig::load();

        PlayStation::Start();

        return true;
    }

    bool Shutdown() {
        PlayStation::Stop();
        return true;
    }

    void SetThreadState(DWORD slot, const XINPUT_STATE* state) {
        t_override_slot = slot;
        t_override_state = state;
    }

    void SetPadFilter(PadFilter filter) {
        g_pad_filter = filter;
    }

    bool XboxMenuOpen() {
        return g_pXboxContext && g_pXboxContext->isOpen();
    }

    WORD RecentButtons(DWORD slot, DWORD maxAgeMs) {
        if (slot >= 4) return 0;
        if (GetTickCount64() - g_last_read[slot].load() > maxAgeMs) return 0;
        return g_last_buttons[slot].load();
    }

    int PressedButton(const XINPUT_STATE& state) {
        const auto& pad = state.Gamepad;
        if (pad.bLeftTrigger > 30) return CGamepadMapping::LeftTrigger;
        if (pad.bRightTrigger > 30) return CGamepadMapping::RightTrigger;
        static const struct { WORD bit; CGamepadMapping::eButton button; } k_buttons[] = {
            {XINPUT_GAMEPAD_DPAD_UP, CGamepadMapping::DpadUp},
            {XINPUT_GAMEPAD_DPAD_DOWN, CGamepadMapping::DpadDown},
            {XINPUT_GAMEPAD_DPAD_LEFT, CGamepadMapping::DpadLeft},
            {XINPUT_GAMEPAD_DPAD_RIGHT, CGamepadMapping::DpadRight},
            {XINPUT_GAMEPAD_START, CGamepadMapping::Start},
            {XINPUT_GAMEPAD_BACK, CGamepadMapping::Back},
            {XINPUT_GAMEPAD_LEFT_THUMB, CGamepadMapping::LeftThumb},
            {XINPUT_GAMEPAD_RIGHT_THUMB, CGamepadMapping::RightThumb},
            {XINPUT_GAMEPAD_LEFT_SHOULDER, CGamepadMapping::LeftShoulder},
            {XINPUT_GAMEPAD_RIGHT_SHOULDER, CGamepadMapping::RightShoulder},
            {XINPUT_GAMEPAD_A, CGamepadMapping::A},
            {XINPUT_GAMEPAD_B, CGamepadMapping::B},
            {XINPUT_GAMEPAD_X, CGamepadMapping::X},
            {XINPUT_GAMEPAD_Y, CGamepadMapping::Y},
        };
        for (auto& b : k_buttons)
            if (pad.wButtons & b.bit) return b.button;
        return -1;
    }

    // XInputGetState on a disconnected slot triggers device enumeration and can
    // stall for milliseconds, so GetXInputGetState refuses to poll empty slots.
    // Empty slots are re-probed one per 500ms (round-robin) so a single frame
    // never eats more than one slow probe; WM_DEVICECHANGE (see Window.cpp)
    // triggers an immediate full rescan so hot-plug is picked up at once, and
    // a failed poll of a connected slot drops it from the mask on the spot.
    // The probes call g_pXInputGetState, the hook's trampoline (the real
    // function, not XInputGetStateDetour), as the wrapper consults this mask.
    // Present (Input::Update) and the game's input (get_key_state) both poll
    // pads, so the cache below is kept under one lock - held only to read and
    // update it, never across a probe (an empty slot's can take milliseconds).
    // g_seen counts each slot's successful reads (probes and polls): a failed
    // poll that started before a newer success doesn't drop the slot.
    static SRWLOCK g_pad_lock = SRWLOCK_INIT;
    static DWORD g_connected_mask = 0;
    static ULONGLONG g_last_probe = 0;
    static DWORD g_next_probe_slot = 0;
    static DWORD g_seen[4] = {};
    static bool g_probing = false; // one probe batch at a time; callers meanwhile use the cached mask
    static volatile LONG g_rescan_requested = 1;  // full sweep on first use

    void RequestPadRescan() {
        InterlockedExchange(&g_rescan_requested, 1);
        PlayStation::RequestRescan();
    }

    DWORD RealConnectedMask() {
        AcquireSRWLockShared(&g_pad_lock);
        DWORD mask = g_connected_mask;
        ReleaseSRWLockShared(&g_pad_lock);
        return mask;
    }

    static DWORD ConnectedPadMask() {
        if (!g_pXInputGetState) return 0;

        DWORD probe = 0; // the slots to probe: all four on a rescan, else one empty slot every 500 ms
        DWORD seen[4];   // each slot's success count as the probes begin
        AcquireSRWLockExclusive(&g_pad_lock);
        auto now = GetTickCount64();
        if (g_probing) {
            // a rescan asked for now stays pending for the next caller
        } else if (InterlockedExchange(&g_rescan_requested, 0)) {
            probe = 0xF;
            g_last_probe = now;
        } else if (now - g_last_probe >= 500) {
            g_last_probe = now;
            for (DWORD n = 0; n < 4; ++n) {
                DWORD i = (g_next_probe_slot + n) % 4;
                if (g_connected_mask & (1u << i))
                    continue;
                probe = 1u << i;
                g_next_probe_slot = (i + 1) % 4;
                break;
            }
        }
        g_probing = g_probing || probe != 0;
        memcpy(seen, g_seen, sizeof(seen));
        DWORD mask = g_connected_mask;
        ReleaseSRWLockExclusive(&g_pad_lock);
        if (!probe) return mask;

        DWORD found = 0;
        for (DWORD i = 0; i < 4; ++i) {
            XINPUT_STATE state;
            if ((probe & (1u << i)) && g_pXInputGetState(i, &state) == ERROR_SUCCESS)
                found |= 1u << i;
        }
        AcquireSRWLockExclusive(&g_pad_lock);
        for (DWORD i = 0; i < 4; ++i) {
            DWORD bit = 1u << i;
            if (!(probe & bit)) continue;
            if (found & bit) {
                g_connected_mask |= bit;
                ++g_seen[i];
            } else if (g_seen[i] == seen[i]) { // not read successfully since the probes began
                g_connected_mask &= ~bit;
            }
        }
        g_probing = false;
        mask = g_connected_mask;
        ReleaseSRWLockExclusive(&g_pad_lock);
        return mask;
    }

    // A DualShock 4/DualSense in the slot, when no XInput controller is there.
    static bool GetPlayStationState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!PlayStation::GetState(dwUserIndex, pState)) {
            memset(pState, 0, sizeof(XINPUT_STATE));
            return false;
        }
        RememberRead(dwUserIndex, pState);
        return true;
    }

    bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState) {
        if (!g_pXInputGetState || !pState) return false;
        memset(pState, 0, sizeof(XINPUT_STATE));
        if (dwUserIndex >= 4)
            return false;
        if (!(ConnectedPadMask() & (1u << dwUserIndex)))
            return GetPlayStationState(dwUserIndex, pState);
        AcquireSRWLockShared(&g_pad_lock);
        DWORD seen = g_seen[dwUserIndex];
        ReleaseSRWLockShared(&g_pad_lock);
        if (g_pXInputGetState(dwUserIndex, pState) != ERROR_SUCCESS) {
            AcquireSRWLockExclusive(&g_pad_lock);
            if (g_seen[dwUserIndex] == seen) // no newer success while this poll ran
                g_connected_mask &= ~(1u << dwUserIndex);
            ReleaseSRWLockExclusive(&g_pad_lock);
            return GetPlayStationState(dwUserIndex, pState);
        }
        AcquireSRWLockExclusive(&g_pad_lock);
        ++g_seen[dwUserIndex];
        g_connected_mask |= 1u << dwUserIndex; // read just now, whatever an older failure cleared
        ReleaseSRWLockExclusive(&g_pad_lock);
        RememberRead(dwUserIndex, pState);
        return true;
    }

    void SetState(DWORD dwUserIndex, XINPUT_VIBRATION *pVibration) {
        if (dwUserIndex < 4 && !(RealConnectedMask() & (1u << dwUserIndex)) &&
            PlayStation::SetVibration(dwUserIndex, pVibration))
            return;
        if (!g_pXInputSetState) return;
        g_pXInputSetState(dwUserIndex, pVibration);
    }

    bool Update() {
        static bool b_pressed     = false;
        static WORD prevButtons[4] = {};
        static bool stickActive[4][4] = {};  // per pad: up, down, left, right
        static bool captureArmed  = false;
        static bool wasCapturing  = false;

        // keyboard nav state — tracks rising edge for each key
        struct NavKey { int vk; InputCommand cmd; bool wasDown; };
        static NavKey navKeys[] = {
            {'W',       InputCommand::Up,     false},
            {VK_UP,     InputCommand::Up,     false},
            {'S',       InputCommand::Down,   false},
            {VK_DOWN,   InputCommand::Down,   false},
            {'A',       InputCommand::Left,   false},
            {VK_LEFT,   InputCommand::Left,   false},
            {'D',       InputCommand::Right,  false},
            {VK_RIGHT,  InputCommand::Right,  false},
            {VK_RETURN, InputCommand::Select, false},
            {VK_ESCAPE, InputCommand::Back,   false},
        };

        // Every controller drives the menus, so any player can open the Xbox
        // menu (on their own page) and change their settings. A missing
        // controller reads as neutral, and keyboard nav below keeps working
        // with no gamepad plugged in.
        XINPUT_STATE states[4];
        WORD buttons[4], justPressed[4];
        WORD anyButtons = 0, anyJustPressed = 0;
        for (DWORD p = 0; p < 4; ++p) {
            GetXInputGetState(p, &states[p]);
            buttons[p] = states[p].Gamepad.wButtons;
            justPressed[p] = buttons[p] & ~prevButtons[p];
            prevButtons[p] = buttons[p];
            anyButtons |= buttons[p];
            anyJustPressed |= justPressed[p];
        }

        // configurable debug UI keyboard key; not while typing in a debug UI
        // text field, where Window.cpp lets the key through to type instead
        static bool debugKeyWasDown = false;
        int debugKey = g_menuConfig.debugKeyboardVKey;
        bool debugKeyIsDown = (GetAsyncKeyState(debugKey) & 0x8000) != 0;
        bool typing = AlphaRing::Global::Global()->show_imgui && ImGui::GetIO().WantTextInput;
        if (debugKeyIsDown && !debugKeyWasDown && !typing)
            AlphaRing::Render::ImGui::ToggleAdvancedMenu(true);
        debugKeyWasDown = debugKeyIsDown;

        bool capturing = g_pXboxContext && g_pXboxContext->isOpen() && g_pXboxContext->isCapturing();
        if (!capturing) {
            // configurable debug UI combo, on any controller
            WORD debugCombo = g_menuConfig.debugComboMask;
            for (int p = 0; p < 4; ++p) {
                if ((buttons[p] & debugCombo) == debugCombo && (justPressed[p] & debugCombo) != 0) {
                    AlphaRing::Render::ImGui::ToggleAdvancedMenu(false);
                    return false;
                }
            }

            // configurable xbox menu combo, on any controller
            WORD menuCombo = g_menuConfig.controllerComboMask;
            for (int p = 0; p < 4; ++p) {
                if ((buttons[p] & menuCombo) == menuCombo && (justPressed[p] & menuCombo) != 0) {
                    AlphaRing::Render::ImGui::ToggleXboxMenu(p);
                    break;
                }
            }
        }

        // navigation and input consumption while Xbox menu is open
        if (g_pXboxContext && g_pXboxContext->isOpen()) {
            capturing = g_pXboxContext->isCapturing();
            if (capturing) {
                // Waiting for a button: let go of everything first, so the
                // press that started it isn't taken, then the first button
                // pressed on an allowed controller is the answer.
                if (!wasCapturing) captureArmed = false;
                int allowed = g_pXboxContext->capturePad(); // -1 = any
                bool held = false;
                for (int p = 0; p < 4; ++p)
                    held |= (allowed < 0 || allowed == p) && PressedButton(states[p]) >= 0;
                if (!captureArmed) {
                    captureArmed = !held;
                } else {
                    for (int p = 0; p < 4; ++p) {
                        if (allowed >= 0 && allowed != p) continue;
                        int button = PressedButton(states[p]);
                        if (button >= 0) {
                            g_pXboxContext->captureButton(p, button);
                            captureArmed = false;
                            break;
                        }
                    }
                }
                for (auto& k : navKeys) {
                    bool isDown = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
                    if (isDown && !k.wasDown && k.vk == VK_ESCAPE) g_pXboxContext->cancelCapture();
                    k.wasDown = isDown;
                }
                wasCapturing = true;
                return true;
            }
            wasCapturing = false;

            // d-pad / face button navigation
            const auto send = [&](WORD btn, InputCommand cmd) {
                if (anyJustPressed & btn) g_pXboxContext->handleInput(cmd);
            };
            send(XINPUT_GAMEPAD_DPAD_UP,    InputCommand::Up);
            send(XINPUT_GAMEPAD_DPAD_DOWN,  InputCommand::Down);
            send(XINPUT_GAMEPAD_DPAD_LEFT,  InputCommand::Left);
            send(XINPUT_GAMEPAD_DPAD_RIGHT, InputCommand::Right);
            send(XINPUT_GAMEPAD_A,          InputCommand::Select);
            send(XINPUT_GAMEPAD_B,          InputCommand::Back);

            // left stick navigation — edge-triggered on deadzone crossing
            const SHORT dz = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
            for (int p = 0; p < 4; ++p) {
                const auto& pad = states[p].Gamepad;
                bool now[4] = {pad.sThumbLY > dz, pad.sThumbLY < -dz, pad.sThumbLX < -dz, pad.sThumbLX > dz};
                const InputCommand cmds[4] = {InputCommand::Up, InputCommand::Down, InputCommand::Left, InputCommand::Right};
                for (int d = 0; d < 4; ++d) {
                    if (now[d] && !stickActive[p][d]) g_pXboxContext->handleInput(cmds[d]);
                    stickActive[p][d] = now[d];
                }
            }

            // keyboard navigation via polling
            for (auto& k : navKeys) {
                bool isDown = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
                if (isDown && !k.wasDown) g_pXboxContext->handleInput(k.cmd);
                k.wasDown = isDown;
            }

            return true; // consume all remaining controller input
        }

        // reset edge-tracking state when menu is closed
        wasCapturing = false;
        memset(stickActive, 0, sizeof(stickActive));
        for (auto& k : navKeys) k.wasDown = false;

        // thumbstick mouse control for debug UI, from any controller
        if (AlphaRing::Global::Global()->show_imgui) {
            const auto f_normalize = [](SHORT sThumb) -> float {
                const auto deadZone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
                return (abs(sThumb) > deadZone) ? (sThumb / 32767.0f) : 0.0f;
            };
            const auto speed = 5.0f;

            ImGuiIO& io = ImGui::GetIO();

            if (anyButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) {
                if (!b_pressed) {
                    io.MouseDown[0] = true;
                    b_pressed = true;
                }
            } else if (b_pressed) {
                io.MouseDown[0] = false;
                b_pressed = false;
            }

            ImVec2 move(0, 0);
            for (int p = 0; p < 4; ++p)
                move += ImVec2(f_normalize(states[p].Gamepad.sThumbRX) * speed, -f_normalize(states[p].Gamepad.sThumbRY) * speed);
            io.MousePos += move;
        }

        return true;
    }
}
