#pragma once

#include "CStateMachine.h"

#include "draw/CXboxRect.hpp"
#include "draw/CXboxButton.hpp"
#include "draw/CXboxPage.hpp"
#include "draw/CXboxIcon.hpp"

#include "assets/colors.h"
#include "assets/mccIcon.h"

#include <algorithm>
#include <optional>
#include <cmath>
#include <string>

#include "mcc/settings/MenuSettings.h"

// The chosen subpage entry's label. The value comes from the shared settings,
// which the advanced menu edits too, so it may point past the entries.
static const char* subOptionLabel(const Option& opt, int index) {
    if (index < 0 || index >= (int)opt.subOptions.size())
        return "None";
    return opt.subOptions[index].label.c_str();
}

void renderMenu(
    int WINDOW_WIDTH,
    int WINDOW_HEIGHT,
    const State& state,
    ImFont* font
) {
    float alpha = (std::min)(state.time / state.duration, 1.0f);

    float menuScale = 0.0f;
    int menuWidth = 0;
    int menuHeight = 0;
    int menuPosX = 0;
    int menuPosY = 0;
    // int unitSize = WINDOW_WIDTH * WINDOW_HEIGHT / 22217;
    // int unitSize = 75 + ((WINDOW_HEIGHT / 1080) * 35);
    int unitSize = 75 + ((WINDOW_HEIGHT / 720) * 45);
    float buttonOffset = 0.0f;
    float fontSize = 25.0f + ((WINDOW_HEIGHT / 1080.0f) * 3.5f);

    int iconSize = 45;
    int pageCount = State().menu.MAX_PAGES;

    ImU8 globalAlpha = 255;

    // -------------------------
    // MENU SCALE / ANIMATION
    // -------------------------
    
    if (state.phase == Phase::Opening) {
        menuScale = 0.45f * alpha;
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = (alpha > 0.5f)
            ? WINDOW_HEIGHT * menuScale
            : WINDOW_HEIGHT * menuScale * 0.5f;
    }
    else if (state.phase == Phase::Closing) {
        menuScale = 0.45f * (1.0f - alpha);
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = (alpha < 0.5f)
            ? WINDOW_HEIGHT * menuScale
            : WINDOW_HEIGHT * menuScale * 0.5f;
    }
    else {
        menuScale = 0.45f;
        menuWidth = WINDOW_WIDTH * menuScale;
        menuHeight = WINDOW_HEIGHT * menuScale;
    }

    menuPosX = (WINDOW_WIDTH - menuWidth) / 2;
    menuPosY = (WINDOW_HEIGHT - menuHeight) / 2;

    // -------------------------
    // GLOBAL ALPHA
    // -------------------------
    if (state.phase == Phase::FadeIn) {
        globalAlpha = static_cast<ImU8>(255 * alpha);
    }
    else if (
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn ||
        state.phase == Phase::ShiftOut
    ) {
        globalAlpha = static_cast<ImU8>(255 * (1.0f - alpha));
    }

    // -------------------------
    // BUTTON OFFSET (SUB MENU)
    // -------------------------
    if (state.phase == Phase::InShiftUp) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]) + ((menuHeight / 7) * alpha);
    }
    else if (state.phase == Phase::InShiftDown) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]) - ((menuHeight / 7) * alpha);
    }
    else if (
        state.phase == Phase::InIdle ||
        state.phase == Phase::InFadeIn
    ) {
        buttonOffset = -((menuHeight / 7) * state.subOptionWindow[0]);
    }

    // -------------------------
    // OPTION OFFSET (MAIN MENU)
    // -------------------------
    float mainButtonOffset = 0.0f;
    if (state.phase == Phase::ShiftUp) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]) + ((menuHeight / 7) * alpha);
    }
    else if (state.phase == Phase::ShiftDown) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]) - ((menuHeight / 7) * alpha);
    }
    else if (
        state.phase == Phase::Idle ||
        state.phase == Phase::FadeIn ||
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn
    ) {
        mainButtonOffset = -((menuHeight / 7) * state.optionWindow[0]);
    }

    // -------------------------
    // PAGE TRANSITION OFFSETS
    // -------------------------
    auto pageOffset = [&](bool rightSide) -> int {
        int updateSize = (menuWidth / 9);

        // if(state.phase == Phase::ShiftRight) {
        //     offset = -updateSize * (alpha) + 50;
        //     extra = 1;
        // }
        // testing unit size in place of +/- 50
        if (state.phase == Phase::ShiftRight)
            return rightSide ? (-updateSize * alpha - (menuWidth / 9)) : (-updateSize * alpha + (menuWidth / 9));

        if (state.phase == Phase::ShiftLeft)
            return rightSide ? (updateSize * alpha - (menuWidth / 9)) : (updateSize * alpha + (menuWidth / 9));

        if (state.phase == Phase::ShiftIn)
            return rightSide ? (-updateSize * alpha * (alpha * 4)) : (updateSize * alpha * (alpha * 4));

        if (state.phase == Phase::ShiftOut)
            return rightSide ? (updateSize * alpha - (menuWidth / 9)) : (-updateSize * alpha + (menuWidth / 9));
                
        if(state.phase == Phase::InShiftUp) {
            return rightSide ? 0 : ((((menuHeight / 7)*state.subOptionWindow[0]) * -1)) + ((menuHeight / 7) * alpha);
        }

        if(state.phase == Phase::InShiftDown) {
            return rightSide ? 0 : ((((menuHeight / 7)*state.subOptionWindow[0]) * -1)) - ((menuHeight / 7) * alpha);
        }

        if(state.phase == Phase::InIdle || state.phase == Phase::InFadeIn) 
            return rightSide ? 0 : ((menuHeight / 7)*state.subOptionWindow[0]) * -1;

        return 0;
    };

    // -------------------------
    // LEFT PAGES
    // -------------------------
    if (
        state.phase != Phase::Opening &&
        state.phase != Phase::Closing &&
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown
    ) {
        int offset = pageOffset(false);
        int prefixSize = -(menuWidth / 9);

        int extra = (state.phase == Phase::ShiftRight || state.phase == Phase::ShiftLeft) ? 1 : 0;

        for (int i = state.pageIndex - 1 + extra; i >= 0; --i) {
            drawPage(
                offset + menuPosX + prefixSize,
                menuPosY,
                menuWidth,
                menuHeight,
                state.menu.pages[i].label.data(),
                false,
                255,
                font,
                fontSize
            );
            prefixSize -= (menuWidth / 9);
        }
    }

    // -------------------------
    // ICON + TITLE
    // -------------------------
    if(state.phase != Phase::Closing && state.phase != Phase::Opening) {   
        drawText(
            menuPosX - (menuWidth * 0.35f),
            menuPosY - ((menuHeight/5) * 3),
            menuWidth,
            menuHeight,
            font,
            25.0f + ((WINDOW_HEIGHT / 1080.0f) * 15.0f),
            IM_COL32(255, 255, 255, 255),
            "MCC Guide"
        );
        ImDrawList* draw = ImGui::GetForegroundDrawList();
        drawIcon(
            draw,
            menuPosX + (menuWidth * 0.85f),
            menuPosY - ((menuHeight/15) * 3.5f),
            (unitSize*0.75f), 
            (unitSize*0.75f),
            mccIcon_png, 
            mccIcon_png_len,
            255
        );
    }
    

    // -------------------------
    // CURRENT PAGE (MAIN)
    // -------------------------
    const auto& page = state.menu.pages[state.pageIndex];
    if(
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown &&
        state.phase != Phase::ShiftOut
    ) {
        drawPage(
            menuPosX,
            menuPosY,
            menuWidth,
            menuHeight,
            page.label.data(),
            true,
            globalAlpha,
            font,
            fontSize
        );
    } else {
        drawPage(
            menuPosX,
            menuPosY,
            menuWidth,
            menuHeight,
            page.options[state.optionIndex].label.data(),
            true,
            globalAlpha,
            font,
            fontSize
        ); 
    }

    // -------------------------
    // RIGHT PAGES
    // -------------------------
    if (state.phase != Phase::Opening &&
        state.phase != Phase::Closing &&
        state.phase != Phase::InIdle &&
        state.phase != Phase::InFadeIn &&
        state.phase != Phase::InShiftUp &&
        state.phase != Phase::InShiftDown &&
        state.pageIndex <= pageCount - 1) {

        int offset = pageOffset(true);
        int suffixSize = menuWidth + (menuWidth / 9);

        int extra = (state.phase == Phase::ShiftRight || state.phase == Phase::ShiftLeft) ? 1 : 0;

        for (int i = state.pageIndex + 1 - extra; i < pageCount; ++i) {
            drawPage(
                offset + menuPosX + suffixSize,
                menuPosY,
                menuWidth,
                menuHeight,
                state.menu.pages[i].label.data(),
                false,
                255,
                font,
                fontSize
            );
            suffixSize += (menuWidth / 9);
        }
    }

    // -------------------------
    // BACKGROUND
    // -------------------------
    drawGradientRect(
        menuPosX,
        menuPosY,
        menuWidth,
        menuHeight,
        IM_COL32(255, 255, 255, 255),
        IM_COL32(255, 255, 255, 255),
        true
    );

    // -------------------------
    // OPTIONS (MAIN PAGE)
    // -------------------------
    if (
        state.phase == Phase::Idle ||
        state.phase == Phase::FadeIn ||
        state.phase == Phase::ShiftRight ||
        state.phase == Phase::ShiftLeft ||
        state.phase == Phase::ShiftIn ||
        state.phase == Phase::ShiftUp ||
        state.phase == Phase::ShiftDown
    ) {
        ImDrawList* mainDraw = ImGui::GetForegroundDrawList();

        mainDraw->PushClipRect(
            ImVec2((float)menuPosX, (float)menuPosY),
            ImVec2((float)(menuPosX + menuWidth),
                (float)(menuPosY + menuHeight)),
            true
        );

        float buttonCount = 0.0f;
        int groupIndex = 0;

        for (int i = 0; i < page.options.size(); i++) {
            const auto& opt = page.options[i];
            OptionType type = opt.type;

            int yBase = menuPosY + buttonCount * (menuHeight / 7) + mainButtonOffset;

            if (type == OptionType::Increment ||
                type == OptionType::Decrement ||
                type == OptionType::PointerDisplay) {

                int displayValue = (state.pageIndex == 0)
                    ? state.menuState.playerCount
                    : state.menuState.sensitivity[state.pageIndex - 1];
                // a player's sensitivity 0 is MCC's own
                std::string displayText = (state.pageIndex != 0 && displayValue <= 0) ? "MCC" : std::to_string(displayValue);

                drawButton(
                    menuPosX + groupIndex * (menuWidth / 3),
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    displayText.c_str(),
                    type,
                    0
                );
                groupIndex = (groupIndex + 1) % 3;
                if (type == OptionType::Increment) {
                    buttonCount++;
                }
                continue;
            }

            if (type == OptionType::Boolean) {
                bool boolValue = false;
                switch (opt.subOptionType) {
                    case Menu::k_useKMOption:             boolValue = state.menuState.useKM; break;
                    case Menu::k_usePlayer0ProfileOption: boolValue = state.menuState.usePlayer0Profile; break;
                    case Menu::k_anyoneCanPauseOption:    boolValue = state.menuState.anyoneCanPause; break;
                    case Menu::k_nativePlayStationOption: boolValue = state.menuState.nativePlayStation; break;
                }

                drawButton(
                    menuPosX,
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    opt.label.c_str(),
                    type,
                    boolValue ? 1 : 0
                );
                buttonCount++;
                continue;
            }

            if (type == OptionType::Cycle && state.pageIndex > 0) {
                int player = state.pageIndex - 1;
                int value = opt.subOptionType == Menu::k_cycleLook   ? state.menuState.invert[player]
                          : opt.subOptionType == Menu::k_cycleFlight ? state.menuState.invertFlight[player]
                                                                     : state.menuState.vibration[player];
                const char* text;
                if (value < 0)
                    text = "MCC Setting";
                else if (opt.subOptionType == Menu::k_cycleVibration)
                    text = value ? "On" : "Off";
                else
                    text = value ? "Inverted" : "Normal";
                std::string label = opt.label + ": " + text;
                drawButton(menuPosX, yBase, menuWidth, menuHeight, font, fontSize, state.optionIndex == i,
                           globalAlpha, label.c_str(), type, 0);
                buttonCount++;
                continue;
            }

            if (type == OptionType::TeamToggle) {
                drawButton(
                    menuPosX,
                    yBase,
                    menuWidth,
                    menuHeight,
                    font,
                    fontSize,
                    state.optionIndex == i,
                    globalAlpha,
                    opt.label.c_str(),
                    type,
                    state.menuState.teamIndex[state.pageIndex - 1]
                );
                buttonCount++;
                continue;
            }

            if (type == OptionType::Subpage) {
                // if (opt.subOptionType > 0) {
                //     // get the index of the player color from the player index
                //     int colorIndex = state.menuState.playerColors[state.pageIndex - 1].colors[state.menu.pages[state.pageIndex].options[i].subOptionType - 1];
                //     ImU32 color = defaultColors[colorIndex];
                //     ImU32 finalColor = IM_COL32(GetRValue(color), GetGValue(color), GetBValue(color), globalAlpha);
                //     drawButton(
                //         menuPosX,
                //         yBase,
                //         menuWidth,
                //         menuHeight,
                //         font,
                //         fontSize,
                //         state.optionIndex == i,
                //         globalAlpha,
                //         opt.label.c_str(),
                //         type,
                //         0,
                //         finalColor
                //     );
                //     buttonCount++;
                //     continue;
                // } else {
                //     // if not a color type, its a controller type
                //     drawButton(
                //         menuPosX,
                //         yBase,
                //         menuWidth,
                //         menuHeight,
                //         font,
                //         fontSize,
                //         state.optionIndex == i,
                //         globalAlpha,
                //         opt.subOptions[state.menuState.controllerIndex[state.pageIndex - 1]].label.c_str(),
                //         type,
                //         0
                //     );
                //     buttonCount++;
                //     continue;
                // }

                switch(opt.subOptionType) {
                    int colorIndex;
                    ImU32 color;
                    ImU32 finalColor;
                    case 0:
                    drawButton(
                        menuPosX,
                        yBase,
                        menuWidth,
                        menuHeight,
                        font,
                        fontSize,
                        state.optionIndex == i,
                        globalAlpha,
                        subOptionLabel(opt, state.menuState.controllerIndex[state.pageIndex - 1]),
                        type,
                        0
                    );
                    break;
                    case 1:
                    case 2:
                    case 3:
                    colorIndex = state.menuState.playerColors[state.pageIndex - 1].colors[state.menu.pages[state.pageIndex].options[i].subOptionType - 1];
                    if (colorIndex < 0 || colorIndex >= 30) {
                        // none picked: the player's look (or MCC's) decides
                        std::string label = opt.label + ": My Look";
                        drawButton(menuPosX, yBase, menuWidth, menuHeight, font, fontSize, state.optionIndex == i,
                                   globalAlpha, label.c_str(), type, 0);
                        break;
                    }
                    color = defaultColors[colorIndex];
                    finalColor = IM_COL32((color >> IM_COL32_R_SHIFT) & 0xFF, (color >> IM_COL32_G_SHIFT) & 0xFF,
                                          (color >> IM_COL32_B_SHIFT) & 0xFF, globalAlpha);
                    drawButton(
                        menuPosX,
                        yBase,
                        menuWidth,
                        menuHeight,
                        font,
                        fontSize,
                        state.optionIndex == i,
                        globalAlpha,
                        opt.label.c_str(),
                        type,
                        0,
                        finalColor
                    );
                    break;
                    case 4:
                    {
                        // entry 0 is MCC's layout (profile -1)
                        std::string stringLabel = std::string("Controller Profile: ") + subOptionLabel(opt, state.menuState.controllerProfile[state.pageIndex - 1] + 1);
                        drawButton(
                            menuPosX,
                            yBase,
                            menuWidth,
                            menuHeight,
                            font,
                            fontSize,
                            state.optionIndex == i,
                            globalAlpha,
                            stringLabel.c_str(),
                            type,
                            0
                        );
                    }
                    break;
                    default:
                    drawButton(menuPosX, yBase, menuWidth, menuHeight, font, fontSize, state.optionIndex == i,
                               globalAlpha, opt.label.c_str(), type, 0);
                    break;
                }
                buttonCount++;
                continue;
            }

            drawButton(
                menuPosX,
                yBase,
                menuWidth,
                menuHeight,
                font,
                fontSize,
                state.optionIndex == i,
                globalAlpha,
                opt.label.c_str(),
                type,
                0
            );

            buttonCount++;
        }

        mainDraw->PopClipRect();
    }

    // -------------------------
    // SUB OPTIONS (SUB MENU STATE)
    // -------------------------
    if (
        state.phase == Phase::InIdle ||
        state.phase == Phase::InFadeIn ||
        state.phase == Phase::ShiftOut ||
        state.phase == Phase::InShiftUp ||
        state.phase == Phase::InShiftDown
    ) {
        ImDrawList* draw = ImGui::GetForegroundDrawList();

        draw->PushClipRect(
            ImVec2((float)menuPosX, (float)menuPosY),
            ImVec2((float)(menuPosX + menuWidth),
                (float)(menuPosY + menuHeight)),
            true
        );

        float buttonCount = 0.0f;

        const auto& subOpts =
            page.options[state.optionIndex].subOptions;

        for (int i = 0; i < subOpts.size(); i++) {
            const auto& opt = subOpts[i];
            OptionType type = state.menu.pages[state.pageIndex].options[state.optionIndex].subOptions[i].type;
            drawButton(
                menuPosX,
                menuPosY + buttonCount * (menuHeight / 7) + buttonOffset,
                menuWidth,
                menuHeight,
                font,
                fontSize,
                state.subOptionIndex == i,
                globalAlpha,
                opt.label.c_str(),
                type,
                0,
                state.menu.pages[state.pageIndex].options[state.optionIndex].subOptions[i].colorValue
            );

            buttonCount++;
        }
        draw->PopClipRect();
    }
}

// While the menu waits for a button: who has to press what.
inline void renderCapturePrompt(int WINDOW_WIDTH, int WINDOW_HEIGHT, const State& state, ImFont* font) {
    const auto& c = state.capture;
    if (c.kind == CaptureKind::None) return;

    std::string title, detail;
    switch (c.kind) {
        case CaptureKind::Controller:
        case CaptureKind::AllControllers:
            title = "Player " + std::to_string(c.player + 1);
            detail = "Press any button on your controller";
            break;
        case CaptureKind::Button:
            title = std::string(AlphaRing::MenuSettings::ProfileActionName(c.action));
            detail = c.pad >= 0 ? "Player " + std::to_string(c.player + 1) + ": press the button for it"
                                : "Press the button for it";
            break;
        default:
            break;
    }
    int secondsLeft = (int)(10.0f - c.time + 0.99f);
    std::string hint = "Esc to cancel  (" + std::to_string(secondsLeft < 0 ? 0 : secondsLeft) + ")";

    ImDrawList* draw = ImGui::GetForegroundDrawList();
    float w = WINDOW_WIDTH * 0.42f, h = WINDOW_HEIGHT * 0.22f;
    float x = (WINDOW_WIDTH - w) * 0.5f, y = (WINDOW_HEIGHT - h) * 0.5f;
    draw->AddRectFilled(ImVec2(0, 0), ImVec2((float)WINDOW_WIDTH, (float)WINDOW_HEIGHT), IM_COL32(0, 0, 0, 120));
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(245, 245, 245, 250), 8.0f);
    draw->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h * 0.32f), IM_COL32(9, 117, 6, 255), 8.0f, ImDrawFlags_RoundCornersTop);
    float big = 25.0f + ((WINDOW_HEIGHT / 1080.0f) * 10.0f);
    float hintSize = 20.0f + ((WINDOW_HEIGHT / 1080.0f) * 4.0f);
    drawText((int)x, (int)y, (int)w, (int)(h * 0.32f), font, big, IM_COL32(255, 255, 255, 255), title.c_str());
    drawText((int)x, (int)(y + h * 0.32f), (int)w, (int)(h * 0.40f), font, big, IM_COL32(0, 0, 0, 255), detail.c_str());
    drawText((int)x, (int)(y + h * 0.70f), (int)w, (int)(h * 0.28f), font, hintSize, IM_COL32(90, 90, 90, 255), hint.c_str());
}
