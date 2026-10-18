// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The frame the client is drawing: its index, its time, and whether one is open at all.

#include "runtime/Frame.hpp"

#include <windows.h>

#include <cstdint>

namespace
{
    // The client paints on its main thread and nowhere else, so none of this is shared.
    bool     g_open      = false;
    uint64_t g_index     = 0;   ///< frames opened since the process started
    int64_t  g_lastBegin = 0;   ///< performance counter at the previous Begin, 0 before the first
    float    g_delta     = 0.0f;

    /// Seconds since the previous Begin, from the performance counter, 0 for the first frame.
    float Elapsed()
    {
        static const int64_t frequency = []
        {
            LARGE_INTEGER value{};
            QueryPerformanceFrequency(&value);
            return value.QuadPart;
        }();

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        const int64_t previous = g_lastBegin;
        g_lastBegin            = now.QuadPart;
        if (!previous || frequency <= 0) return 0.0f;
        return static_cast<float>(static_cast<double>(now.QuadPart - previous) /
                                  static_cast<double>(frequency));
    }
}

namespace wxl::core::frame
{
    bool Begin(wxl_frame_args& out)
    {
        if (g_open) return false;
        g_open  = true;
        g_delta = Elapsed();
        ++g_index;
        out.index = g_index;
        out.delta = g_delta;
        return true;
    }

    bool End(wxl_frame_args& out)
    {
        if (!g_open) return false;
        g_open    = false;
        out.index = g_index;
        out.delta = g_delta;
        return true;
    }

    bool Current(wxl_frame_args& out)
    {
        if (!g_open) return false;
        out.index = g_index;
        out.delta = g_delta;
        return true;
    }

    void Reset() { g_open = false; }
}
