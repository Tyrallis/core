// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// WarcraftXL.dll on x64: how the recompiled client runs a wxl.point.

#ifndef WXL_CORE_POINTS_H
#define WXL_CORE_POINTS_H

// Each point of wxl/points.def is a WXL_POINT_<n>(...) line at the head of its function in the
// fork (client/WarcraftXL/Points.gen.h). The line tests the point's byte in the client's own
// array, which WarcraftXLPoints hands over and the runtime sets while a mod is attached; set, it
// hands wxl_core_point_enter the addresses of the arguments, a slot for the result and a function
// that calls the original. Nothing here exists on x86, where a point is a detour.

#include <stdint.h>

#include "wxl/abi.h"
#include "wxl/core/init.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Calls the point's function as the client would have, with *args[i] as argument i and the
/// answer written to *result (NULL for void), skipping the point once on this thread.
typedef void(WXL_CALL* wxl_core_point_original)(void* const* args, void* result);

/// The client's point table: one row per point, "name: lowered signature", and one byte per
/// point the runtime sets while a mod is attached.
typedef struct wxl_core_points
{
    uint32_t           struct_size;
    uint32_t           count;
    const char* const* rows;  ///< WXL_POINT_ROWS of Points.gen.h
    volatile uint8_t*  armed; ///< count bytes, all zero, in the client's image, for its life
} wxl_core_points;

/**
 * @brief Hands the runtime the client's point table. Call before WarcraftXLInit, which loads the
 *        mods that may attach.
 *
 * @return WXL_OK; WXL_E_INVALID when the table is null or its rows differ from the runtime's (the
 *         log names the first that differs: rebuild both from one tree), in which case no point
 *         can be attached and the client runs as if none were listed.
 */
WXL_CORE_API wxl_result WXL_CALL WarcraftXLPoints(const wxl_core_points* points);

/**
 * @brief Runs point @p point: its Before handlers, its Replace chain ending at @p original, its
 *        After handlers. Called by the WXL_POINT_<n> line while the point's byte is set.
 */
WXL_CORE_API void WXL_CALL wxl_core_point_enter(uint32_t point, void* const* args, void* result,
                                                wxl_core_point_original original);

#ifdef __cplusplus
}
#endif

#endif // WXL_CORE_POINTS_H
