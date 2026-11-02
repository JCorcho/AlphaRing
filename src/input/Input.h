#pragma once

#include <Windows.h>
#include <Xinput.h>

namespace AlphaRing {
    namespace Input {
        bool Init();
        bool Shutdown();
        bool Update();
        // Safe for per-frame polling: skips disconnected slots (cached
        // connected mask) to avoid XInputGetState's stall on empty slots,
        // and always zeroes *pState when returning false. A slot without an
        // XInput controller may hold a DualShock 4/DualSense (PlayStation.h).
        bool GetXInputGetState(DWORD dwUserIndex, XINPUT_STATE* pState);
        // Full rescan of the controller slots on the next poll; called on
        // WM_DEVICECHANGE so hot-plug is detected at once.
        void RequestPadRescan();
        void SetState(DWORD dwUserIndex, XINPUT_VIBRATION* pVibration);

        // The slots holding a real XInput controller (as last seen).
        DWORD RealConnectedMask();
        // The buttons of the slot's latest successful read, if it was within
        // `maxAgeMs`; else 0.
        WORD RecentButtons(DWORD slot, DWORD maxAgeMs = 250);
        // The button (a CGamepadMapping::eButton) held in `state`, or -1.
        int PressedButton(const XINPUT_STATE& state);

        // Until cleared (null), XInputGetState of `slot` on this thread answers
        // with *state: the game's input for a player then matches what the
        // splitscreen code handed it, however the game reads the controller.
        void SetThreadState(DWORD slot, const XINPUT_STATE* state);
        // Changes MCC's own XInputGetState results (not those above).
        using PadFilter = void (*)(DWORD slot, XINPUT_STATE* state);
        void SetPadFilter(PadFilter filter);

        bool XboxMenuOpen();
    };
}
