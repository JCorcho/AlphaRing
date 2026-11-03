#pragma once

namespace MCC::Splitscreen {
    bool Initialize();
    void ImGuiContext();

    // Halo CE's and Halo 2's Anniversary renderers only ever draw two views, stacked: with 3-4 players
    // the others got no view (Halo 2: player 2's view across the bottom, players 2 and 3's HUDs over it).
    // Such missions start in Classic graphics, which lays every view out: bit 0 of the first game-options
    // byte picks Anniversary visuals in both engines, and it is cleared only while the engine copies the
    // options, leaving MCC's own setting untouched. Back still switches to Anniversary in the mission.
    // (From kirklandsig's upstream PR #40, without its side-by-side layout.)
    class ClassicGraphicsScope {
    public:
        // `game`: CGameGlobal::Halo1 or Halo2
        ClassicGraphicsScope(unsigned char* game_options, int game);
        ~ClassicGraphicsScope();

    private:
        unsigned char* m_options = nullptr;
        unsigned char m_saved = 0;
    };
}