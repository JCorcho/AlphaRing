#pragma once

#include "render/imgui/ICContext.h"
#include <SDL_mixer.h>
#include "CStateMachine.h"

#include <array>
#include <optional>

class CXboxContext : public ICContext {
public:
    CXboxContext(ImFont* font);
    ~CXboxContext();

    // `pad`: the controller that opened it, whose player's page shows first (-1: keyboard).
    void open(int pad = -1);
    void close();
    bool isOpen() const;

    void handleInput(InputCommand cmd);

    // Waiting for a button press (assigning a controller, binding a button).
    bool isCapturing() const;
    int capturePad() const; // -1 = any controller
    void captureButton(int pad, int button);
    void cancelCapture();
    void render() override;

private:
    Menu m_menu;
    std::array<Mix_Chunk*, 6> m_sounds;
    std::optional<StateMachine> m_stateMachine;
    ImFont* m_font;
    Uint32 m_lastTick = 0;

    // Hands the menu's state back to the shared settings and saves them.
    void commitState();
};

extern CXboxContext* g_pXboxContext;
