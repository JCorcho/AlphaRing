#include "D3d11.h"

#include "common.h"

#include "global/Global.h"
#include "mcc/CGameGlobal.h"

namespace AlphaRing::Render::D3d11 {
    static const char* GameName(CGameGlobal::eGame game) {
        switch (game) {
            case CGameGlobal::Halo1: return "Halo1";
            case CGameGlobal::Halo2: return "Halo2";
            case CGameGlobal::Halo3: return "Halo3";
            case CGameGlobal::Halo4: return "Halo4";
            case CGameGlobal::GroundHog: return "GroundHog";
            case CGameGlobal::Halo3ODST: return "Halo3ODST";
            case CGameGlobal::HaloReach: return "HaloReach";
            default: return "Unknown";
        }
    }

    // #23's composite clip (release 1.3.8), which clipped each Halo 2 Anniversary view quad to its player's
    // half, was removed: the renderer places the quads itself, so it squeezed each view into a quarter.
    // Its black-screen case is covered by halo2/anniversary.cpp's two-view fix (Splitscreen options).
    void ResetCompositeFrameCounter() {}

    DefDetourFunction(HRESULT, __stdcall, DrawIndexed, ID3D11DeviceContext* p_context,
                      const UINT IndexCount, const UINT StartIndexLocation, const INT  BaseVertexLocation) {
        if (AlphaRing::Global::Global()->wireframe) {
            Graphics()->SetWireframe();
#ifdef NEW_WIREFRAME
            D3D11_PRIMITIVE_TOPOLOGY topology;
            p_context->IAGetPrimitiveTopology(&topology);

            p_context->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
            ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);

            p_context->IASetPrimitiveTopology(topology);
#endif
        }

        auto pGameGlobal = GameGlobal();

        {
            static CGameGlobal::eGame last_logged_game = (CGameGlobal::eGame)-1;
            if (pGameGlobal != nullptr && pGameGlobal->current_game != last_logged_game) {
                last_logged_game = pGameGlobal->current_game;
                LOG_INFO("D3D11: active game changed to {}", GameName(last_logged_game));
            }
        }

        return ppOriginal_DrawIndexed(p_context, IndexCount, StartIndexLocation, BaseVertexLocation);
    }

    bool CreateHook() {
        return Hook::Detour({
            {GetFunction(73), DrawIndexed, (void **) &ppOriginal_DrawIndexed},
        });
    }
}
