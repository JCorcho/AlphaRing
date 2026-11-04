<img width="1920" height="315" alt="AlphaRing Logo" src="https://github.com/user-attachments/assets/04c51c7b-3b3d-4cde-a094-bc690d5a97fc" />

A Splitscreen Mod for Halo the Master Chief Collection

[![Build status](https://ci.appveyor.com/api/projects/status/o3qbtc7jirw81xmb?svg=true)](https://ci.appveyor.com/project/WinterSquire/alpharing)
[![](https://dcbadge.limes.pink/api/server/https://discord.gg/TUyAnCrpuz)](https://discord.gg/TUyAnCrpuz)

### Showcase

| | |
|--|--|
| Splitscreen in Every Game <br> <img width="1086" height="651" alt="image" src="https://github.com/user-attachments/assets/0b5a5109-f602-424f-a9ec-51e35da832b8" /> | LAN Mutliplayer Support <img width="1569" height="974" alt="image" src="https://github.com/user-attachments/assets/6255730f-e1e7-495b-afb8-152e93d0d070" /> |
| Camera Tool (H3) <br> ![Camera](https://github.com/WinterSquire/AlphaRing/assets/135317392/d359b2e8-5302-430f-be0d-bc065e63f546) | Object Browser (H3) <br> ![Object](https://github.com/WinterSquire/AlphaRing/assets/135317392/0bce1af7-354f-4d9d-92f7-eb2d46d8ae37) |
| 8 Players Campaign <br> ![Splitscreen 8 players](https://github.com/WinterSquire/AlphaRing/assets/135317392/7d9f4281-892a-47e2-8e0c-845a965e5d11) | Splitscreen With [Mod](https://steamcommunity.com/sharedfiles/filedetails/?id=3153235187) (By [Priception](https://steamcommunity.com/id/priception)) <br> ![H4](https://github.com/WinterSquire/AlphaRing/assets/135317392/5359868c-c5db-4300-9805-84c61b0bd8ee) |

### Features
* Splitscreen (all games)
* "Xbox 360" Menu (controller-friendly, toggled with F4/Left Stick Click + Start)
* Advanced Menu (for greater customization, toggled with F1)
* Camera Tool (H3)
* Object Browser (H3)

### Installation
Good news!  We now have a launcher for quickly setting up the mod.  You can find [the repository here](https://github.com/megabitt01/alpharing-launcher-v2).
For a quick guide, watch this YouTube video: https://www.youtube.com/watch?v=E_0IjFBD2pI&t=30s

#### Manual Installation
Make sure you have the latest [Microsoft Visual C++ Redistributable](https://aka.ms/vs/17/release/vc_redist.x64.exe) installed.

Download the latest stable build from the [Releases](https://github.com/kirklandsig/AlphaRing/releases) page.

Place the DLL into the "Halo The Master Chief Collection\mcc\binaries\win64" directory and launch the game with EAC off.

**Xbox app / Game Pass:** place the DLL into `C:\XboxGames\Halo- The Master Chief Collection\Content\mcc\binaries\win64` (or the same path under your Xbox games folder) and launch the game from the "Halo: MCC Anti-Cheat Disabled (Mods and Limited Services)" Start menu entry.

For Running on Steam Deck/Linux, add the following command in the Steam Game Launch Options:
```
WINEDLLOVERRIDES="WTSAPI32=n,b" %command%
```

Works with **any Proton version** (Proton 9.0, Proton Experimental, Proton GE, etc.)

1. **Configure the game**:
   - Right-click MCC → Properties → Compatibility
   - Enable "Force the use of a specific Steam Play compatibility tool"
   - Select any Proton version (Proton 9.0, Experimental, or Proton GE all work)

2. **Set launch options**: Add the following to Steam Launch Options:
   ```
   WINEDLLOVERRIDES="WTSAPI32=n,b" %command%
   ```

3. **Controller Setup (Important for non-Xbox controllers)**:
   - For 8BitDo and other third-party controllers, enable **Steam Input** for the controller
   - Go to Steam → Settings → Controller → Enable "Xbox Configuration Support"
   - This allows Steam to translate your controller inputs to XInput, which MCC and AlphaRing expect
   - Without this, some buttons (like A or stick clicks) may not be detected

> **Note:** Tested on Batocera Linux with the unofficial Batocera add-ons Steam client.

### Usage
Toggle menu: `F4` or `Start` + `Left Stick Click` on any controller (advanced menu: `F1` or `Start` + `Right Stick Click`)

To navigate using Controller use the `Right Stick` to move the mouse and `RB` to click.

When the menu is open, game input is disabled.

### Splitscreen Guide (per-player settings)

Open the Xbox menu with `F4` or `Start + Left Stick Click` **on any controller**. Opened from a controller, it starts on that player's page, so each player can set up their own controls. `A` selects, `B` goes back (out of a list first, then closes and applies).

**Splitscreen page**
- **Add/Remove Player** - number of players (1-4).
- **Assign Controllers** - each player in turn presses any button on their controller.
- **Use K/M for Player 1**, **Copy Player 1's Look** (players 2-4 wear player 1's armor; controls stay their own).
- **Anyone Can Pause** - Start on any player's controller opens the pause menu (sends Escape when player 1 is on keyboard/mouse).
- **PlayStation Controllers** - DualShock 4 / DualSense support (see below).

**Player pages** - everything here is per player. "MCC" means "use what's set in MCC's own settings".
- **Controller** - which controller this player uses, or *Press a Button to Assign*.
- **Controller Profile** - *MCC Settings* (the buttons from MCC's controller settings), the presets (Default, Southpaw, Boxer, Green Thumb, Bumper Jumper, Recon) or your own profiles.
- **Edit My Buttons** - pick an action, then press the button you want for it on your controller. The first edit copies the preset into a profile of your own ("<name>'s Controls"), so the presets and the other player's controls never change. Actions not in the list (Sprint, vehicle and Forge controls...) keep MCC's buttons.
- **Sensitivity** (`MCC` or 1-20), **Look** and **Flying** inversion, **Vibration** (MCC / Off / On).
- **Stick Deadzone** - an extra deadzone (5-25%) for a drifting stick; the rest of the stick is rescaled so it still reaches full speed.
- **Primary/Secondary/Tertiary Color** - a fixed color, or *From My Look / MCC*.
- **Look & Armor** - per game:
  - *Copy My Look From MCC Now* (during a campaign mission or a match): this player takes the armor, colors and service tag currently set in MCC's Customize menu for that game.
  - *Use MCC's Look*, *Save My Look as a Character*, *Wear: <character>*.

**Giving each player their own armor:** MCC only has one profile, so set it up one player at a time. Player 2 sets MCC's Customize menu to their look, starts a mission (or match), and uses *Look & Armor → Copy My Look From MCC Now* on their page. Afterwards set MCC's customization back to player 1's look (player 1 follows MCC unless they copy a look too). Each look is saved per game, so this is done once per game. If a new look doesn't appear right away, it shows after the next checkpoint load, respawn or mission restart. The advanced menu (`F1` → Splitscreen → a player → Armor) has full per-piece armor pickers too.

**3-4 players in Halo CE or Halo 2 with the mode above off - fallback:** the games' own Anniversary renderer draws two views only. `F1` → Splitscreen → Options → *Halo CE/2: Classic graphics with 3-4 players* (off by default) starts such missions in Classic graphics, where everyone gets a view; Back (Tab) still switches mid-mission.

**Campaign colors (experimental, on by default):** `F1` → Splitscreen → Options → *Halo CE/2: campaign colors* paints each player's body in their colors in the Halo CE and Halo 2 campaigns - Halo CE in Classic graphics; Halo 2 only if the campaign Chief's armor takes colors at all (an open question, see below). It also tints multiplayer bodies, so turn it off for team games there.

**Colors in campaign (what the games do themselves):** the campaigns of Halo CE, Halo 2, Halo 3 and ODST never show player colors - every co-op player is a fixed character (in MCC itself too), so neither MCC's customization nor the colors picked here change them. Colors and armor picked here show where the game uses them: every game's multiplayer, and the campaigns of Reach and Halo 4.

**Campaign:** everything above works the same in campaign missions as in multiplayer. What a game shows of a player's customization in co-op is up to that game (Reach shows the full Spartan armor; games with fixed characters, such as Halo 3's Arbiter and Elites, show less or nothing). For colors, *Copy My Look From MCC* is the most reliable way, as it takes MCC's exact values instead of the menu's 30-color table.

**PlayStation controllers (no DS4Windows needed):** turn the controller on (USB or Bluetooth-paired in Windows) and it shows up as the next free controller, with rumble and its lightbar in the player's color (blue, red, green, pink). Each controller gets the same slot back every time (also after restarting MCC), so each player keeps their controller; the slots are remembered in `alpha_ring_controllers.cfg`. The battery level shows in the Controller list, and a notice warns below 20%. While DS4Windows runs, the controllers are left to it.

**Halo 2 Anniversary split screen shows each view a quarter high with a black band between?** That was the view clip added in release 1.3.8 (#23), now removed. If Halo 2 Anniversary split screen instead shows a **black** screen for you (seen under Proton), turn on `F1` → Splitscreen → Options → *Halo 2: two-view black-screen fix*.

**3-4 players in Halo CE / Halo 2 Anniversary graphics (experimental, on by default):** those two renderers draw two views, so the mod draws all four: two per frame, in turn (players 1-2, then 3-4), so each view updates at half the frame rate. Every player gets their own view, HUD, first-person weapon and zoom. With three players, Halo 2 gives player 1 the wide top half; Halo CE uses quarters and leaves the fourth black. Takes effect as a mission starts; Back (Tab) still switches graphics mid-mission. Switch: `F1` → Splitscreen → Options → *Halo CE/2: Anniversary graphics with 3-4 players*. From [kirklandsig/AlphaRing](https://github.com/kirklandsig/AlphaRing).

**Checking a setup:** `F1` → Splitscreen → *Controller Test* shows every controller slot live (what kind of controller, which player has it, sticks, triggers and buttons) and what each player's settings last gave the game. If a controller's buttons arrive twice, another program is also translating it: close DS4Windows, or disable Steam Input for MCC (MCC → Properties → Controller → *Disable Steam Input*), or turn **PlayStation Controllers** off to keep using those.

---

## Building from Source

### Prerequisites
- Visual Studio 2022 Build Tools
- CMake 3.27+

### Build Commands
```bash
# First time setup
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64

# Build
"C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe" WTSAPI32.vcxproj -p:Configuration=Release -p:Platform=x64
```

Output: `build/Release/WTSAPI32.dll`

---

## Credits
- **Original AlphaRing:** [WinterSquire](https://github.com/WinterSquire/AlphaRing)
- **Controller Binding and Proton Fixes Fork:** [kirklandsig](https://github.com/kirklandsig/AlphaRing)
- [Assembly](https://github.com/XboxChaos/Assembly) for the tag group research.
- [Blender](https://github.com/blender/blender) for the bezier curve calculation.
- [Priception](https://github.com/Priception) for adding UI controller support and helping with the interface and crash issue.
