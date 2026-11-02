#pragma once

#include <Windows.h>
#include <Xinput.h>

// DualShock 4 and DualSense controllers read straight from their HID reports
// (USB or Bluetooth) and handed to the game as XInput controllers in the free
// XInput slots, so they work without DS4Windows or Steam Input. Rumble and the
// lightbar (in the player's color) are sent back to them.
namespace AlphaRing::Input::PlayStation {
    void Start();                    // the background thread; once, from Input::Init
    void Stop();
    void SetEnabled(bool enabled);   // off closes the controllers, freeing their slots
    bool Enabled();
    void RequestRescan();            // a device arrived or left (WM_DEVICECHANGE)

    // The slot's controller, if one of these sits in it. `guide` adds the PS
    // button as XInput's Guide button (0x0400), which only XInputGetStateEx reports.
    bool GetState(DWORD slot, XINPUT_STATE* state, bool guide = false);
    DWORD ConnectedMask();
    bool SetVibration(DWORD slot, const XINPUT_VIBRATION* vibration);
    void SetSlotPlayer(DWORD slot, int player); // for the lightbar; -1 for none
    const char* SlotName(DWORD slot);           // "DualSense (Bluetooth)", or null
}
