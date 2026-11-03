#pragma once

#include <string>

// A player's three armor colors, each an AlphaRing color (0-29, see
// assets/colors.h) or -1 to keep the color from the player's look (their
// copied MCC look, else the MCC profile's own).
struct PlayerColor {
    int colors[3];
};

// The per-player settings both menus edit. "MCC" values (-1, or 0 for the
// sensitivity) leave the setting to the MCC profile, so a player who never
// changes anything here plays with their own MCC settings.
struct MenuState {
    int playerCount;
    bool useKM;
    int controllerIndex[4];   // 0-3 = controller slot, 4 = none
    int controllerProfile[4]; // -1 = the MCC button layout, else a custom profile
    int sensitivity[4];       // 0 = MCC's, else 1-20
    int invert[4];            // look: -1 = MCC's, 0 = normal, 1 = inverted
    int teamIndex[4];
    PlayerColor playerColors[4];
    bool usePlayer0Profile;   // players 2-4 wear player 1's look ("Copy Player 1's Look")

    // file version 9
    int invertFlight[4];      // -1 = MCC's, 0 = normal, 1 = inverted
    int vibration[4];         // -1 = MCC's, 0 = off, 1 = on
    bool anyoneCanPause;      // Start on any player's controller pauses the game
    bool nativePlayStation;   // DualShock 4 / DualSense read directly (USB and Bluetooth)
    int deadzone[4];          // extra stick deadzone in percent, 0 = none (for drifting sticks)
    bool halo2CompositeClip;  // #23's Halo 2 Anniversary view-quad clip (off: see Global.h)
    bool classicGraphics3P;   // Halo CE/2 missions with 3-4 players start in Classic graphics
};
