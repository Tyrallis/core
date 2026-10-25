// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.point: the handlers attached to the engine's functions, and the dispatch that runs them.

#pragma once

#include "wxl/core/points.h"
#include "wxl/services/point.h"

#include <cstddef>
#include <cstdint>

// Shared by both runtimes. What differs is how a point is reached, which each architecture's
// realisation supplies through Arm, Disarm and Has (PointsX86.cpp: a detour at the function's
// address; PointsX64.cpp: the byte the WXL_POINT_<n> line of the fork tests), and how it calls
// in: Enter, with the arguments by address and a function that runs the original.

namespace wxl::core::points
{
    /// The points of wxl/points.def, in id order (an id is 1 + the index).
    enum Index : uint32_t
    {
#define WXL_POINT(Ident, name, ret, params, lowered, layout_hash, policy, thread, stability, runtimes) k##Ident,
#include "wxl/points.def"
#undef WXL_POINT
        kCount
    };

    /// One row of wxl/points.def, as the runtime reads it.
    struct Row
    {
        const char* name;
        const char* lowered;
        uint32_t    layout_hash;
        uint32_t    thread;      ///< WXL_POINT_THREAD_*
        uint32_t    flags;       ///< WXL_POINT_ON_X86, _ON_X64, _REPLACEABLE, _STABLE
        uint32_t    arg_count;
        bool        returns;     ///< whether it answers something
    };

    const Row& RowOf(uint32_t index);

    /// What the generic handler shape is handed as the original: wxl_core_point_original.
    using Original = wxl_core_point_original;

    /**
     * @brief Runs a point: its Before handlers, its Replace chain ending at @p original, its
     *        After handlers. With nothing attached, the original alone.
     *
     * @param args    args[i] is the address of argument i, `self` first; what Before and Replace
     *                handlers write there is what the original is called with.
     * @param result  the address of the answer, null for void.
     */
    void Enter(uint32_t index, void* const* args, void* result, Original original);

    /**
     * @brief Runs a point's Before handlers at an inlined copy of its function (x86 mid-function
     *        hooks): no Replace, no After, flagged WXL_POINT_CALL_INLINED.
     */
    void EnterInlined(uint32_t index, void* const* args);

    /// The wxl.point table, WXL_POINT_SERVICE version WXL_POINT_SERVICE_VERSION.
    const wxl_point_api& Api();

    /// Detaches every handler a mod attached, when it is disabled or unloaded.
    void ReleaseOf(uint32_t mod);

    /// The same for every mod, on the way out.
    void ReleaseAll();

    /// Logs the points this runtime has and why it lacks the others, at start-up.
    void Describe();

    // --- What an architecture supplies ------------------------------------------------------

    /// Whether this runtime has a point; when not, @p why receives the reason find gives.
    bool Has(uint32_t index, const char** why);

    /// Hooks a point: its first handler is being attached. False refuses the attach, with the
    /// reason in @p why.
    bool Arm(uint32_t index, char* why, size_t capacity);

    /// Unhooks a point: its last handler went.
    void Disarm(uint32_t index);
}
