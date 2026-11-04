// Halo CE campaign colors: each player's body in the color picked for them (the Xbox menu's, else MCC's). The
// game sets a body's colors from the player only in multiplayer; a campaign Chief keeps his model's green.
// actor_customize_unit (0xC01DD0) shows where a unit keeps them: four RGB change colors at +0x144 of its object
// data, mirrored at +0x174, which the Classic renderer reads every frame. They're written after a player spawns
// and every second (a checkpoint revert brings the saved body back).
#include "halo1.h"

#include "global/Global.h"
#include "mcc/CGameGlobal.h"
#include "mcc/settings/MenuSettings.h"
#include "log/Log.h"

#include <cstring>

namespace Halo1::Entry::Colors {
    EntryFeature("Halo CE campaign colors", OFFSET_HALO1_PV_LOCAL_PLAYERS, OFFSET_HALO1_PF_OBJECT_TRY_AND_GET);

    constexpr int kChangeColors = 0x144, kChangeColorsMirror = 0x174, kSlots = 4;
    constexpr unsigned kUnitTypes = 1 << 0 | 1 << 1; // bipeds and vehicles

    static bool s_logged[4];

    void Apply(__int64 module) {
        if (!AlphaRing::Global::MCC::Splitscreen()->b_campaign_colors) return;
        // players globals: int16 local player count +0xB4, player handle[4] +0xB8, unit[4] +0xC8
        char* globals = *(char**)(module + OFFSET_HALO1_PV_LOCAL_PLAYERS);
        if (globals == nullptr) return;
        for (int local = 0; local < 4; ++local) {
            int unit = *(int*)(globals + 0xC8 + 4 * local);
            if (unit == -1) continue;
            float rgb[3];
            if (!AlphaRing::MenuSettings::PlayerColorRGB(local, 0, CGameGlobal::Halo1, rgb)) continue;
            char* data = ((char* (*)(int, unsigned))(module + OFFSET_HALO1_PF_OBJECT_TRY_AND_GET))(unit, kUnitTypes);
            if (data == nullptr) continue;
            float* colors = (float*)(data + kChangeColors);
            float* mirror = (float*)(data + kChangeColorsMirror);
            bool same = true;
            for (int k = 0; k < kSlots && same; ++k) same = memcmp(colors + 3 * k, rgb, sizeof rgb) == 0;
            if (same) continue;
            for (int k = 0; k < kSlots; ++k) {
                memcpy(colors + 3 * k, rgb, sizeof rgb);
                memcpy(mirror + 3 * k, rgb, sizeof rgb);
            }
            if (!s_logged[local]) {
                s_logged[local] = true;
                LOG_INFO("Halo CE campaign colors: player {} body {:#x} colored ({:.2f}, {:.2f}, {:.2f})", local + 1, unit, rgb[0], rgb[1], rgb[2]);
            }
        }
    }

    Halo1Entry(entry_spawn, OFFSET_HALO1_PF_PLAYER_SPAWN, void, player_spawn, int player) {
        ((player_spawn_t)entry_spawn.m_pOriginal)(player);
        Apply(entry_spawn.m_target - entry_spawn.m_offset);
    }

    Halo1Entry(entry_update, OFFSET_HALO1_PF_PLAYERS_UPDATE, void, players_update, void* state) {
        ((players_update_t)entry_update.m_pOriginal)(state);
        static int s_tick = 0;
        if (++s_tick % 30 == 0) Apply(entry_update.m_target - entry_update.m_offset);
    }

    void Reset() { memset(s_logged, 0, sizeof s_logged); }
    const bool s_reset = (Halo1EntrySet()->on_remove(&Reset), true);
}
