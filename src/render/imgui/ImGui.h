#pragma once

namespace AlphaRing::Render::ImGui {
    bool Initialize();

    void Render();

    // The Xbox menu (F4) and the advanced menu (F1) are never open together:
    // opening one closes the other. The advanced menu waits for the Xbox
    // menu's close animation, which commits its settings, before it shows.
    // `pad`: the controller that asked (its player's page opens), -1 for the keyboard.
    void ToggleXboxMenu(int pad = -1);
    void ToggleAdvancedMenu(bool showMouse);
}