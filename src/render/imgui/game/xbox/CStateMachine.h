#pragma once

#include <SDL_mixer.h>
#include "CXboxMenu.hpp"
#include "CXboxMenuState.h"

#include <deque>
#include <array>
#include <string>
#include <vector>

// -------------------------
// Input abstraction (replaces SDL_Event)
// -------------------------
enum class InputCommand {
    Left,
    Right,
    Up,
    Down,
    Select,
    Back
};

// -------------------------
// State definitions
// -------------------------
enum class Phase {
    Opening,
    PostOpening,
    FadeIn,
    InFadeIn,
    Idle,
    InIdle,
    ShiftIn,
    ShiftOut,
    ShiftRight,
    ShiftLeft,
    ShiftUp,
    ShiftDown,
    InShiftUp,
    InShiftDown,
    PreClosing,
    Closing
};

// -------------------------
// Waiting for a button press
// -------------------------
enum class CaptureKind {
    None,
    Controller,      // a player's controller: the first controller pressed
    AllControllers,  // every player's in turn
    Button,          // a button of a player's own controls
};

struct Capture {
    CaptureKind kind = CaptureKind::None;
    int player = 0;
    int action = 0;  // Button: the profile action
    int pad = -1;    // the controller listened to, -1 = any
    float time = 0;  // seconds waited
    int taken[4] = {-1, -1, -1, -1}; // AllControllers: the controllers assigned so far
};

// -------------------------
// Core runtime state
// -------------------------
struct State {
    Menu menu;
    std::array<Mix_Chunk*, 6> sounds;
    int pageIndex;
    int optionIndex;
    std::deque<int> optionWindow;
    int subOptionIndex;
    std::deque<int> subOptionWindow;
    float time;
    float duration;
    Phase phase;
    MenuState menuState;
    Capture capture;
    std::vector<int> lookCharacters; // the Look subpage's characters, after its fixed entries
};

// -------------------------
// State machine (ImGui-driven)
// -------------------------
class StateMachine {
public:
    StateMachine(const Menu& menu, const std::array<Mix_Chunk*, 6>& sounds);

    void handleLeft();
    void handleRight();
    void handleUp();
    void handleDown();
    void handleOption();
    void handleClose();

    void handleInput(InputCommand cmd);
    void update(float dt);
    void render(int WINDOW_WIDTH, int WINDOW_HEIGHT, ImFont* font);

    bool isRunning() const;
    State& getState();

    void setPage(int page); // opens on this page (0 = Splitscreen, 1-4 = a player's)

    bool isCapturing() const;
    int capturePad() const; // -1 = any controller
    void captureButton(int pad, int button);
    void cancelCapture();

private:
    bool handleSubOptionStays(); // the chosen entry's effect; true to stay on the subpage
    void refreshSubpage();
    void startCapture(CaptureKind kind, int player, int action = 0);
    int playerPad(int player) const;

    void finishShiftUp();
    void finishShiftDown();
    void finishOptionShiftUp();
    void finishOptionShiftDown();

    State currentState;
    bool running;
};