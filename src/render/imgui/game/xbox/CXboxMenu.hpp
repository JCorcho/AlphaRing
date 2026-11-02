#pragma once

#include <imgui.h>
#include "./assets/colors.h"
#include <string>
#include <vector>
#include <optional>
#include <iostream>

// option types:
// - Boolean        a checkbox
// - TeamToggle     red/blue
// - Subpage        opens a list of SubpageOption entries
// - Increment / Decrement / PointerDisplay   a "- value +" row
// - Cycle          steps through values (MCC / Normal / Inverted ...)
// - Action         does something at once

enum class OptionType {
    Boolean,
    TeamToggle,
    Subpage,
    SubpageOption,
    Increment,
    Decrement,
    PointerDisplay,
    Cycle,
    Action
};

class Option {
public:
    std::string label;
    OptionType type;
    std::vector<Option> subOptions;
    int subOptionType = 0;
    int value = 0;
    std::optional<ImU32> colorValue;

    std::optional<int*> pointer;
    
    Option(std::string lbl, OptionType t)
        : label(std::move(lbl)), type(t) {}
    
    Option(std::string lbl, OptionType t, int s)
    : label(std::move(lbl)), type(t), subOptionType(s) {}

    Option(std::string lbl, OptionType t, int s, ImU32 c)
        : label(std::move(lbl)), type(t), subOptionType(s), colorValue(c) {}

    // Option(std::string lbl, std::vector<std::string> subs)
    //     : label(std::move(lbl)),
    //       type(OptionType::Subpage),
    //       subOptions(std::move(subs)) {}

    // Option(std::string lbl, int* ptr)
    //     : label(std::move(lbl)),
    //       type(OptionType::PointerDisplay),
    //       pointer(ptr) {}

    void addOption(const Option& opt) {
        subOptions.push_back(opt);
    }
};

class Page {
public:
    std::string label;
    std::vector<Option> options;
    bool visible = false;

    Page() = default;

    explicit Page(std::string lbl, bool vis = false)
        : label(std::move(lbl)), visible(vis) {}

    void addOption(const Option& opt) {
        options.push_back(opt);
    }
};

// Names of the controller slots, in MenuState::controllerIndex order (4 = no
// controller), then the entry that assigns a controller by a button press.
inline constexpr const char* k_controllerNames[6] = {
    "Controller 1", "Controller 2", "Controller 3", "Controller 4", "None", "Press a Button to Assign",
};

class Menu {
public:
    static constexpr int MAX_PAGES = 5;

    // player page options, in order
    static constexpr int k_controllerOption = 0;
    static constexpr int k_controllerProfileOption = 1;
    static constexpr int k_editButtonsOption = 2;

    // subOptionType of a Subpage and its entries
    static constexpr int k_subController = 0;
    static constexpr int k_subPrimaryColor = 1;   // 1-3: the three colors
    static constexpr int k_subProfile = 4;
    static constexpr int k_subEditButtons = 5;
    static constexpr int k_subLook = 6;

    // subOptionType of the Splitscreen page's Boolean options
    static constexpr int k_useKMOption = 0;
    static constexpr int k_usePlayer0ProfileOption = 1;
    static constexpr int k_anyoneCanPauseOption = 2;
    static constexpr int k_nativePlayStationOption = 3;

    // subOptionType of a player page's Cycle options
    static constexpr int k_cycleLook = 0;
    static constexpr int k_cycleFlight = 1;
    static constexpr int k_cycleVibration = 2;
    static constexpr int k_cycleDeadzone = 3;

    // subOptionType of an Action option
    static constexpr int k_actionAssignControllers = 0;

    // entries of the Look subpage before the saved characters
    static constexpr int k_lookCopy = 0;
    static constexpr int k_lookClear = 1;
    static constexpr int k_lookSave = 2;
    static constexpr int k_lookFirstCharacter = 3;

    Page pages[MAX_PAGES];

    Menu() {
        init();
    }

    // Fills each player's "Controller Profile" subpage: MCC's layout, then
    // the custom profiles, in MenuState::controllerProfile + 1 order.
    void setControllerProfiles(const std::vector<std::string>& names) {
        for (int index = 1; index <= 4; index++) {
            auto& option = pages[index].options[k_controllerProfileOption];
            option.subOptions.clear();
            option.addOption(Option("MCC Settings", OptionType::SubpageOption, k_subProfile));
            for (const auto& name : names)
                option.addOption(Option(name, OptionType::SubpageOption, k_subProfile));
        }
    }

private:
    void addColorOption(int index, const char* label, int subOptionType) {
        Option option(label, OptionType::Subpage, subOptionType);
        option.addOption(Option("From My Look / MCC", OptionType::SubpageOption, subOptionType));
        for (int c = 0; c < 30; c++)
            option.addOption(Option(k_colorNames[c], OptionType::SubpageOption, subOptionType, defaultColors[c]));
        pages[index].addOption(option);
    }

    void initPlayer(int index) {
        pages[index].label = "Player " + std::to_string(index);

        pages[index].addOption(Option("Controller", OptionType::Subpage, k_subController));
        for (const char* name : k_controllerNames)
            pages[index].options[k_controllerOption].addOption(Option(name, OptionType::SubpageOption, k_subController));

        // filled from the custom profiles by setControllerProfiles()
        pages[index].addOption(Option("Controller Profile", OptionType::Subpage, k_subProfile));

        // filled when opened (StateMachine::refreshSubpage)
        pages[index].addOption(Option("Edit My Buttons", OptionType::Subpage, k_subEditButtons));

        pages[index].addOption(Option("Reduce Sensitivity", OptionType::Decrement));
        pages[index].addOption(Option("Display", OptionType::PointerDisplay));
        pages[index].addOption(Option("Increase Sensitivity", OptionType::Increment));

        pages[index].addOption(Option("Look", OptionType::Cycle, k_cycleLook));
        pages[index].addOption(Option("Flying", OptionType::Cycle, k_cycleFlight));
        pages[index].addOption(Option("Vibration", OptionType::Cycle, k_cycleVibration));
        pages[index].addOption(Option("Stick Deadzone", OptionType::Cycle, k_cycleDeadzone));

        pages[index].addOption(Option("Red Team", OptionType::TeamToggle, 0));

        addColorOption(index, "Primary Color", k_subPrimaryColor);
        addColorOption(index, "Secondary Color", k_subPrimaryColor + 1);
        addColorOption(index, "Tertiary Color", k_subPrimaryColor + 2);

        // filled when opened
        pages[index].addOption(Option("Look & Armor", OptionType::Subpage, k_subLook));
    }

    void init() {
        pages[0] = Page("Splitscreen", true);

        pages[0].addOption(Option("Remove Player", OptionType::Decrement));
        pages[0].addOption(Option("Display", OptionType::PointerDisplay));
        pages[0].addOption(Option("Add Player", OptionType::Increment));
        pages[0].addOption(Option("Assign Controllers", OptionType::Action, k_actionAssignControllers));
        pages[0].addOption(Option("Use K/M for Player 1", OptionType::Boolean, k_useKMOption));
        pages[0].addOption(Option("Copy Player 1's Look", OptionType::Boolean, k_usePlayer0ProfileOption));
        pages[0].addOption(Option("Anyone Can Pause", OptionType::Boolean, k_anyoneCanPauseOption));
        pages[0].addOption(Option("PlayStation Controllers", OptionType::Boolean, k_nativePlayStationOption));

        for (int i = 1; i <= 4; i++) {
            initPlayer(i);
        }
    }
};
