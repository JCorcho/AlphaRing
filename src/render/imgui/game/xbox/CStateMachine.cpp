#pragma once

#include "CXboxRender.hpp"
#include "CStateMachine.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include "mcc/CGameManager.h"
#include "mcc/settings/MenuSettings.h"
#include "input/Input.h"
#include "input/PlayStation.h"
#include "render/imgui/Toast.h"
#include <array>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>

// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------
namespace {
    // The main option list groups Increment/Decrement/PointerDisplay options
    // (e.g. Reduce/Display/Increase Sensitivity) into a single visual row, just
    // like the render loop's row counter does. This computes the row that a
    // given raw option index falls on so window-scroll math matches what's
    // actually drawn. Landing on the 2nd/3rd item of such a group must report
    // the same row as the 1st item, not a later one.
    bool isRowGroupType(OptionType t) {
        return t == OptionType::Increment || t == OptionType::Decrement || t == OptionType::PointerDisplay;
    }

    int rowIndexForOption(const Page& page, int optionIndex) {
        int row = 0;
        bool prevWasGroup = false;
        int limit = (std::min)(optionIndex, (int)page.options.size() - 1);
        for (int i = 0; i <= limit; ++i) {
            bool isGroup = isRowGroupType(page.options[i].type);
            bool continuesRow = isGroup && prevWasGroup;
            if (i > 0 && !continuesRow) row++;
            prevWasGroup = isGroup;
        }
        return row;
    }
}

// ------------------------------------------------------------
// Constructor
// ------------------------------------------------------------
StateMachine::StateMachine(const Menu& menuInstance, const std::array<Mix_Chunk*, 6>& sounds)
    : currentState{
        menuInstance,
        sounds,
        0,
        0,
        {0,1,2,3,4,5},
        0,
        {0,1,2,3,4,5},
        0,
        0.25f,
        Phase::Opening,
        MenuState{},
        Capture{},
        {}
    },
    running(true)
{
}

// ------------------------------------------------------------
// Input layer (REPLACES SDL_Event handler)
// ------------------------------------------------------------
void StateMachine::handleInput(InputCommand cmd)
{
    if (cmd == InputCommand::Up && currentState.phase == Phase::InShiftUp) {
        finishShiftUp();
        currentState.phase = Phase::InIdle;
        currentState.time = 0;
        handleUp();
        return;
    }

    if (cmd == InputCommand::Down && currentState.phase == Phase::InShiftDown) {
        finishShiftDown();
        currentState.phase = Phase::InIdle;
        currentState.time = 0;
        handleDown();
        return;
    }

    if (cmd == InputCommand::Up && currentState.phase == Phase::ShiftUp) {
        finishOptionShiftUp();
        currentState.phase = Phase::Idle;
        currentState.time = 0;
        handleUp();
        return;
    }

    if (cmd == InputCommand::Down && currentState.phase == Phase::ShiftDown) {
        finishOptionShiftDown();
        currentState.phase = Phase::Idle;
        currentState.time = 0;
        handleDown();
        return;
    }

    if (!(currentState.phase == Phase::Idle ||
          currentState.phase == Phase::InIdle ||
          currentState.phase == Phase::PostOpening ||
          currentState.phase == Phase::PreClosing)) {
        return;
    }

    switch (cmd) {
        case InputCommand::Left:  handleLeft(); break;
        case InputCommand::Right: handleRight(); break;
        case InputCommand::Up:    handleUp(); break;
        case InputCommand::Down:  handleDown(); break;
        case InputCommand::Select: handleOption(); break;
        case InputCommand::Back:
            if (currentState.phase == Phase::InIdle) { // B leaves a subpage first
                Mix_PlayChannel(-1, currentState.sounds[1], 0);
                currentState.phase = Phase::ShiftOut;
                currentState.time = 0;
            } else {
                handleClose();
            }
            break;
    }
}

// ------------------------------------------------------------
// Navigation
// ------------------------------------------------------------
void StateMachine::handleLeft() {
    if (currentState.phase != Phase::Idle) return;

    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    if (opt.type == OptionType::Increment) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        currentState.optionIndex = (std::max)(0, currentState.optionIndex - 2);
    } else {
        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        if (currentState.pageIndex > 0) {
            currentState.phase = Phase::ShiftLeft;
            currentState.time = 0;
        }
    }
}

void StateMachine::handleRight() {
    if (currentState.phase != Phase::Idle) return;

    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    if (opt.type == OptionType::Decrement) {
        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        currentState.optionIndex = (std::min)(
            (int)currentState.menu.pages[currentState.pageIndex].options.size() - 1,
            currentState.optionIndex + 2
        );
    } else {
        if (currentState.pageIndex >= State().menu.MAX_PAGES - 1)
            return;

        Mix_PlayChannel(-1, currentState.sounds[3], 0);
        currentState.phase = Phase::ShiftRight;
        currentState.time = 0;
    }
}

void StateMachine::handleUp() {
    if (currentState.phase == Phase::Idle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        auto& opt = currentState.menu.pages[currentState.pageIndex]
                .options[currentState.optionIndex];

        if (opt.type == OptionType::Increment) {
            currentState.optionIndex = (std::min)(
                (int)currentState.menu.pages[currentState.pageIndex].options.size() - 1,
                currentState.optionIndex - 3
            );
        } else {
            currentState.optionIndex = (std::max)(0, currentState.optionIndex - 1);
        }
        
        int row = rowIndexForOption(currentState.menu.pages[currentState.pageIndex], currentState.optionIndex);
        if (row < currentState.optionWindow[0]) {
            currentState.phase = Phase::ShiftUp;
            currentState.time = 0;
        }
    }

    if (currentState.phase == Phase::InIdle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);
        currentState.subOptionIndex = (std::max)(0, currentState.subOptionIndex - 1);

        if (currentState.subOptionIndex < currentState.subOptionWindow[0]) {
            Mix_PlayChannel(-1, currentState.sounds[2], 0);
            currentState.phase = Phase::InShiftUp;
            currentState.time = 0;
        }
    }
}

void StateMachine::handleDown() {
    if (currentState.phase == Phase::Idle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);

        int maxOption = (int)currentState.menu.pages[currentState.pageIndex].options.size();

        auto& opt = currentState.menu.pages[currentState.pageIndex]
                        .options[currentState.optionIndex];

        if (opt.type == OptionType::Decrement) {
            currentState.optionIndex = (std::min)(maxOption - 1, currentState.optionIndex + 3);
        } else {
            currentState.optionIndex = (std::min)(maxOption - 1, currentState.optionIndex + 1);
        }

        int row = rowIndexForOption(currentState.menu.pages[currentState.pageIndex], currentState.optionIndex);
        int maxWin = (int)currentState.optionWindow.size();
        if (row > currentState.optionWindow[maxWin - 1]) {
            currentState.phase = Phase::ShiftDown;
            currentState.time = 0;
        }
    }

    if (currentState.phase == Phase::InIdle) {
        Mix_PlayChannel(-1, currentState.sounds[2], 0);

        int maxSub = (int)currentState.menu.pages[currentState.pageIndex]
                        .options[currentState.optionIndex]
                        .subOptions.size();

        currentState.subOptionIndex = (std::min)(maxSub - 1, currentState.subOptionIndex + 1);

        int maxWin = (int)currentState.subOptionWindow.size();
        if (currentState.subOptionIndex > currentState.subOptionWindow[maxWin - 1]) {
            currentState.phase = Phase::InShiftDown;
            currentState.time = 0;
        }
    }
}

// ------------------------------------------------------------
// Selection logic (audio replaced)
// ------------------------------------------------------------
namespace Settings = AlphaRing::MenuSettings;

static const char* ButtonName(int button) {
    auto names = CGamepadMapping::ButtonNames();
    return (button >= 0 && button < 16) ? names->at(button) : "None";
}

static std::string PlayerName(int player) {
    auto profile = CGameManager::get_profile(player);
    char name[128] = {};
    if (profile) WideCharToMultiByte(CP_UTF8, 0, profile->name, -1, name, sizeof(name), nullptr, nullptr);
    return name[0] ? name : "Player " + std::to_string(player + 1);
}

int StateMachine::playerPad(int player) const {
    if (player < 0 || player >= 4) return -1;
    if (player == 0 && currentState.menuState.useKM) return -1;
    int pad = currentState.menuState.controllerIndex[player];
    return (pad >= 0 && pad < 4) ? pad : -1;
}

void StateMachine::setPage(int page) {
    currentState.pageIndex = (std::max)(0, (std::min)(page, Menu::MAX_PAGES - 1));
    currentState.optionIndex = 0;
}

bool StateMachine::isCapturing() const {
    return currentState.capture.kind != CaptureKind::None;
}

int StateMachine::capturePad() const {
    return currentState.capture.pad;
}

void StateMachine::startCapture(CaptureKind kind, int player, int action) {
    auto& c = currentState.capture;
    c = Capture{};
    c.kind = kind;
    c.player = player;
    c.action = action;
    // a button of a player's controls comes from their own controller
    c.pad = kind == CaptureKind::Button ? playerPad(player) : -1;
    if (kind == CaptureKind::AllControllers && player == 0 && currentState.menuState.useKM)
        c.player = 1; // player 1 is on keyboard/mouse
}

void StateMachine::closeNow() {
    currentState.capture.kind = CaptureKind::None;
    handleClose();
}

void StateMachine::refreshProfileNames() {
    std::vector<std::string> names;
    for (auto& profile : Settings::Profiles()) names.push_back(profile.name);
    currentState.menu.setControllerProfiles(names);
}

void StateMachine::cancelCapture() {
    if (!isCapturing()) return;
    currentState.capture.kind = CaptureKind::None;
    Mix_PlayChannel(-1, currentState.sounds[5], 0);
}

void StateMachine::captureButton(int pad, int button) {
    auto& c = currentState.capture;
    auto& ms = currentState.menuState;
    switch (c.kind) {
        case CaptureKind::Controller:
            ms.controllerIndex[c.player] = pad;
            c.kind = CaptureKind::None;
            AlphaRing::Toast::Show("Player %d: Controller %d", c.player + 1, pad + 1);
            break;
        case CaptureKind::AllControllers: {
            for (int taken : c.taken)
                if (taken == pad) return; // one controller per player
            ms.controllerIndex[c.player] = pad;
            c.taken[c.player] = pad;
            AlphaRing::Toast::Show("Player %d: Controller %d", c.player + 1, pad + 1);
            c.time = 0;
            if (++c.player >= ms.playerCount)
                c.kind = CaptureKind::None;
            break;
        }
        case CaptureKind::Button: {
            std::lock_guard lock(Settings::Mutex());
            int profile = Settings::OwnProfile(c.player, ms);
            refreshProfileNames(); // it may be a new copy
            auto& profiles = Settings::Profiles();
            if (profile >= 0 && profile < (int)profiles.size()) {
                Settings::ProfileButton(profiles[profile].buttons, c.action) = (CGamepadMapping::eButton)button;
                Settings::Save();
            }
            c.kind = CaptureKind::None;
            refreshSubpage();
            break;
        }
        default:
            return;
    }
    Mix_PlayChannel(-1, currentState.sounds[4], 0);
}

// Fills the subpage being opened (or redrawn) with what it shows now.
void StateMachine::refreshSubpage() {
    if (currentState.pageIndex < 1) return;
    int player = currentState.pageIndex - 1;
    auto& option = currentState.menu.pages[currentState.pageIndex].options[currentState.optionIndex];
    auto& ms = currentState.menuState;
    std::lock_guard lock(Settings::Mutex());

    switch (option.subOptionType) {
        case Menu::k_subController: {
            DWORD real = AlphaRing::Input::RealConnectedMask();
            for (int slot = 0; slot < 4 && slot < (int)option.subOptions.size(); ++slot) {
                std::string label = k_controllerNames[slot];
                std::string name = AlphaRing::Input::PlayStation::SlotName(slot);
                if (!name.empty())
                    label += ": " + name;
                else if (!(real & (1u << slot)))
                    label += " (not connected)";
                option.subOptions[slot].label = label;
            }
            break;
        }
        case Menu::k_subProfile:
            refreshProfileNames();
            break;
        case Menu::k_subEditButtons: {
            int profile = ms.controllerProfile[player];
            auto& profiles = Settings::Profiles();
            ControllerProfileMapping buttons = (profile >= 0 && profile < (int)profiles.size()) ? profiles[profile].buttons
                                                                                                : Settings::MccButtons();
            option.subOptions.clear();
            for (int a = 0; a < Settings::k_profileActionCount; ++a) {
                std::string label = std::string(Settings::ProfileActionName(a)) + ": " + ButtonName(Settings::ProfileButton(buttons, a));
                option.addOption(Option(label, OptionType::SubpageOption, Menu::k_subEditButtons));
            }
            option.addOption(Option("Back to MCC Settings", OptionType::SubpageOption, Menu::k_subEditButtons));
            break;
        }
        case Menu::k_subLook: {
            int game = Settings::CurrentGame();
            int armorGame = Settings::ArmorGameOf(game);
            option.subOptions.clear();
            option.addOption(Option(game >= 0 ? "Copy My Look From MCC Now" : "Copy My Look From MCC (in a mission or match)", OptionType::SubpageOption, Menu::k_subLook));
            option.addOption(Option("Use MCC's Look", OptionType::SubpageOption, Menu::k_subLook));
            option.addOption(Option("Save My Look as a Character", OptionType::SubpageOption, Menu::k_subLook));
            currentState.lookCharacters.clear();
            auto& characters = Settings::Characters();
            for (int i = 0; i < (int)characters.size(); ++i) {
                if (armorGame < 0 || characters[i].game != armorGame) continue;
                currentState.lookCharacters.push_back(i);
                option.addOption(Option("Wear: " + characters[i].name, OptionType::SubpageOption, Menu::k_subLook));
            }
            break;
        }
        default:
            break;
    }
}

bool StateMachine::handleSubOptionStays() {
    auto& option = currentState.menu.pages[currentState.pageIndex].options[currentState.optionIndex];
    int index = currentState.subOptionIndex;
    int player = currentState.pageIndex - 1;
    auto& ms = currentState.menuState;
    if (player < 0 || index < 0 || index >= (int)option.subOptions.size()) return false;

    Mix_PlayChannel(-1, currentState.sounds[4], 0);

    switch (option.subOptionType) {
        case Menu::k_subController:
            if (index == 5)
                startCapture(CaptureKind::Controller, player);
            else
                ms.controllerIndex[player] = index;
            return false;
        case Menu::k_subPrimaryColor:
        case Menu::k_subPrimaryColor + 1:
        case Menu::k_subPrimaryColor + 2:
            ms.playerColors[player].colors[option.subOptionType - Menu::k_subPrimaryColor] = index - 1;
            return false;
        case Menu::k_subProfile:
            ms.controllerProfile[player] = index - 1;
            return false;
        case Menu::k_subEditButtons:
            if (index < Settings::k_profileActionCount) {
                startCapture(CaptureKind::Button, player, index);
            } else {
                // the player's own profile stays in the Controller Profile list
                ms.controllerProfile[player] = -1;
                refreshSubpage();
            }
            return true;
        case Menu::k_subLook: {
            std::lock_guard lock(Settings::Mutex());
            int game = Settings::CurrentGame();
            int armorGame = Settings::ArmorGameOf(game);
            if (index == Menu::k_lookCopy) {
                if (Settings::CaptureLook(player, &ms))
                    AlphaRing::Toast::Show("Player %d now wears MCC's current look (if it doesn't show yet: next checkpoint or respawn)", player + 1);
                else
                    AlphaRing::Toast::Show("Start a mission or match first: the look is copied for the game being played");
            } else if (index == Menu::k_lookClear) {
                Settings::ClearLook(player, &ms);
                AlphaRing::Toast::Show("Player %d wears MCC's look", player + 1);
            } else if (index == Menu::k_lookSave) {
                if (armorGame < 0) {
                    AlphaRing::Toast::Show("Characters are for Halo 2A, 3, Reach and 4 (in other games your copied colors are kept per game)");
                } else {
                    auto profile = CGameManager::get_profile(player);
                    std::string name = Settings::CharacterName(profile ? profile->name : L"Player");
                    Settings::SaveCharacter(name, armorGame, Settings::Armor(player, armorGame),
                                            profile ? profile->name : L"Player", &Settings::LookColors(player, game));
                    AlphaRing::Toast::Show("Saved \"%s\"", name.c_str());
                }
            } else {
                int n = index - Menu::k_lookFirstCharacter;
                if (n >= 0 && n < (int)currentState.lookCharacters.size()) {
                    auto profile = CGameManager::get_profile(player);
                    Settings::LoadCharacter(currentState.lookCharacters[n], player,
                                            profile ? profile->name : nullptr, profile ? (int)std::size(profile->name) : 0);
                    // the character's colors show, not ones picked here
                    for (int& color : ms.playerColors[player].colors) color = -1;
                    AlphaRing::Toast::Show("Player %d wears %s", player + 1, Settings::Characters()[currentState.lookCharacters[n]].name.c_str());
                }
            }
            Settings::Save();
            return false;
        }
        default:
            return false;
    }
}

void StateMachine::handleOption() {
    auto& opt = currentState.menu.pages[currentState.pageIndex]
                    .options[currentState.optionIndex];

    if (currentState.phase == Phase::InIdle) {
        if (!handleSubOptionStays()) {
            currentState.phase = Phase::ShiftOut;
            currentState.time = 0;
        }
        return;
    }

    Mix_PlayChannel(-1, currentState.sounds[4], 0);
    auto& ms = currentState.menuState;
    int player = currentState.pageIndex - 1;

    switch (opt.type) {
        case OptionType::Increment:
            if (currentState.pageIndex == 0) {
                if (ms.playerCount < 4) ms.playerCount++;
            } else if (ms.sensitivity[player] < 20) {
                ms.sensitivity[player]++;
            }
            break;
        case OptionType::Decrement:
            if (currentState.pageIndex == 0) {
                if (ms.playerCount > 1) ms.playerCount--;
            } else if (ms.sensitivity[player] > 0) {
                ms.sensitivity[player]--; // 0 = MCC's
            }
            break;
        case OptionType::Boolean:
            if (currentState.pageIndex == 0) {
                switch (opt.subOptionType) {
                    case Menu::k_useKMOption:              ms.useKM = !ms.useKM; break;
                    case Menu::k_usePlayer0ProfileOption:  ms.usePlayer0Profile = !ms.usePlayer0Profile; break;
                    case Menu::k_anyoneCanPauseOption:     ms.anyoneCanPause = !ms.anyoneCanPause; break;
                    case Menu::k_nativePlayStationOption:  ms.nativePlayStation = !ms.nativePlayStation; break;
                }
            }
            break;
        case OptionType::Cycle:
            if (player >= 0 && opt.subOptionType == Menu::k_cycleDeadzone) {
                // off, 5, 10 ... 25, off: for a drifting stick
                ms.deadzone[player] = ms.deadzone[player] >= 25 ? 0 : ms.deadzone[player] + 5;
            } else if (player >= 0) {
                int* value = opt.subOptionType == Menu::k_cycleLook   ? &ms.invert[player]
                           : opt.subOptionType == Menu::k_cycleFlight ? &ms.invertFlight[player]
                                                                      : &ms.vibration[player];
                *value = *value >= 1 ? -1 : *value + 1; // MCC -> off/normal -> on/inverted -> MCC
            }
            break;
        case OptionType::TeamToggle:
            if (player >= 0) ms.teamIndex[player] = !ms.teamIndex[player];
            break;
        case OptionType::Action:
            if (opt.subOptionType == Menu::k_actionAssignControllers)
                startCapture(CaptureKind::AllControllers, 0);
            break;
        case OptionType::Subpage:
            if (currentState.phase == Phase::Idle) {
                refreshSubpage();
                currentState.subOptionWindow = {0,1,2,3,4,5};
                currentState.phase = Phase::ShiftIn;
                currentState.time = 0;
            }
            break;
        default:
            break;
    }
}

void StateMachine::handleClose() {
    Mix_PlayChannel(-1, currentState.sounds[1], 0);
    currentState.phase = Phase::Closing;
    currentState.time = 0;
}

void StateMachine::finishShiftUp() {
    currentState.subOptionWindow.pop_back();
    currentState.subOptionWindow.push_front(currentState.subOptionWindow.front() - 1);
}

void StateMachine::finishShiftDown() {
    currentState.subOptionWindow.pop_front();
    currentState.subOptionWindow.push_back(currentState.subOptionWindow.back() + 1);
}

void StateMachine::finishOptionShiftUp() {
    currentState.optionWindow.pop_back();
    currentState.optionWindow.push_front(currentState.optionWindow.front() - 1);
}

void StateMachine::finishOptionShiftDown() {
    currentState.optionWindow.pop_front();
    currentState.optionWindow.push_back(currentState.optionWindow.back() + 1);
}

// ------------------------------------------------------------
// UPDATE (unchanged logic)
// ------------------------------------------------------------
void StateMachine::update(float dt)
{
    if (isCapturing()) {
        currentState.capture.time += dt;
        if (currentState.capture.time > 10.0f)
            cancelCapture();
    }

    if (currentState.time < currentState.duration)
        currentState.time += dt;

    if (currentState.time >= currentState.duration) {

        switch (currentState.phase) {

            case Phase::Opening:
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::Closing:
                running = false;
                break;

            case Phase::ShiftRight: {
                currentState.optionIndex = 0;
                currentState.phase = Phase::FadeIn;
                int maxPage = State().menu.MAX_PAGES;
                currentState.pageIndex = (std::min)(maxPage - 1, currentState.pageIndex + 1);
                break;
            }

            case Phase::ShiftLeft:
                currentState.optionIndex = 0;
                currentState.phase = Phase::FadeIn;
                currentState.pageIndex = (std::max)(0, currentState.pageIndex - 1);
                break;

            case Phase::InShiftUp:
                finishShiftUp();
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::InShiftDown:
                finishShiftDown();
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::ShiftUp:
                finishOptionShiftUp();
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::ShiftDown:
                finishOptionShiftDown();
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::FadeIn:
                currentState.optionIndex = 0;
                currentState.optionWindow = {0,1,2,3,4,5};
                currentState.phase = Phase::Idle;
                currentState.time = 0;
                break;

            case Phase::InFadeIn:
                currentState.phase = Phase::InIdle;
                currentState.time = 0;
                break;

            case Phase::ShiftIn:
                currentState.subOptionIndex = 0;
                currentState.phase = Phase::InFadeIn;
                break;

            case Phase::ShiftOut:
                currentState.phase = Phase::FadeIn;
                break;

            default:
                break;
        }
    }
}

// ------------------------------------------------------------
// RENDER (SDL REMOVED → ImGui backend expected elsewhere)
// ------------------------------------------------------------
void StateMachine::render(int WINDOW_WIDTH, int WINDOW_HEIGHT, ImFont* font)
{
    // No SDL. Rendering should be moved into:
    // - ImGui widgets OR
    // - custom ImDrawList rendering layer

    // Example placeholder hook:
    // renderMenuImGui(drawList, currentState);
    renderMenu(WINDOW_WIDTH, WINDOW_HEIGHT, currentState, font);
    renderCapturePrompt(WINDOW_WIDTH, WINDOW_HEIGHT, currentState, font);
}

// ------------------------------------------------------------
// Accessors
// ------------------------------------------------------------
bool StateMachine::isRunning() const {
    return running;
}

State& StateMachine::getState() {
    return currentState;
}