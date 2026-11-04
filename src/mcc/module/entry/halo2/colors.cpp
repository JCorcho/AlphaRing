// Halo 2 campaign colors. As a player spawns, player_spawn (0x69E580) asks 0x68CC40 for the player's four
// placement colors - from the player's four palette bytes (player +0x7C, MCC's profile colors) through MCC's
// palette (0x37960) - and gives them to the new body. In a campaign it answers no, and the body keeps the
// model's own colors. Here it answers with the player's colors anyway: those MCC gave the player, else the ones
// picked in the Xbox menu. Whether the campaign Chief's armor shows them is up to its shaders.
#include "halo2.h"

#include "global/Global.h"
#include "mcc/CGameGlobal.h"
#include "mcc/settings/MenuSettings.h"
#include "log/Log.h"

#include <cstring>

namespace Halo2::Entry::Colors {
    EntryFeature("Halo 2 campaign colors", OFFSET_HALO2_PF_PALETTE_COLOR);

    static bool s_logged[4];

    Halo2Entry(entry_colors, OFFSET_HALO2_PF_PLAYER_COLORS, bool, player_colors, signed char* colors, short team, float* out) {
        bool result = ((player_colors_t)entry_colors.m_pOriginal)(colors, team, out);
        if (result || colors == nullptr || out == nullptr || !AlphaRing::Global::MCC::Splitscreen()->b_campaign_colors)
            return result;
        __int64 module = entry_colors.m_target - entry_colors.m_offset;
        char* player = (char*)colors - 0x7C;
        int local = *(short*)(player + 0x28); // its local index (0x69E4A0 keeps it)
        if (local < 0 || local > 3) return result;

        bool any = false;
        for (int i = 0; i < 4; ++i) {
            float rgb[3];
            if (colors[i] >= 0) {
                float tmp[3];
                const float* palette = ((float* (*)(float*, int))(module + OFFSET_HALO2_PF_PALETTE_COLOR))(tmp, colors[i]);
                memcpy(rgb, palette, sizeof rgb);
            } else if (!AlphaRing::MenuSettings::PlayerColorRGB(local, i < 3 ? i : 0, CGameGlobal::Halo2, rgb)) {
                continue;
            }
            memcpy(out + 3 * i, rgb, sizeof rgb);
            any = true;
        }
        if (any && !s_logged[local]) {
            s_logged[local] = true;
            LOG_INFO("Halo 2 campaign colors: player {} spawns with palette bytes {} {} {} {} -> ({:.2f}, {:.2f}, {:.2f}) ...",
                     local + 1, colors[0], colors[1], colors[2], colors[3], out[0], out[1], out[2]);
        }
        return any;
    }

    void Reset() { memset(s_logged, 0, sizeof s_logged); }
    const bool s_reset = (Halo2EntrySet()->on_remove(&Reset), true);
}
