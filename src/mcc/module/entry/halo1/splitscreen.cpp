// Halo CE's window layout with the Anniversary 3-4 player mode (anniversary.cpp), which draws every view in a
// quarter: three players get the four-player windows, not a half and two quarters. (The hook kirklandsig's
// Left/Right split screen puts here too, without the Left/Right part.)
#include "halo1.h"

namespace Halo1::Entry::Anniversary {
    bool Quad(__int64 module);
}

namespace Halo1::Entry::Splitscreen {
    Halo1Entry(entry_window, OFFSET_HALO1_PF_SPLIT_WINDOW, void, split_window, int view, int views, short* rect,
               short* copy) {
        if (views == 3 && Anniversary::Quad(entry_window.m_target - entry_window.m_offset))
            views = 4;
        ((split_window_t)entry_window.m_pOriginal)(view, views, rect, copy);
    }
}
