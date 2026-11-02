#pragma once

// Short messages drawn at the top of the screen for a few seconds, menus open
// or not ("Player 2's controller disconnected"). Show() is safe on any thread.
namespace AlphaRing::Toast {
    void Show(const char* format, ...);
    bool Active();  // something to draw
    void Draw();    // inside an ImGui frame, on the render thread
}
