// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The frame the client is drawing: its index, its time, and whether one is open at all.

#pragma once

#include "wxl/events.h"

namespace wxl::core::frame
{
    /**
     * @brief Opens a frame, and fills in what FrameBegin carries.
     *
     * FrameBegin and FrameEnd come from two seams -- the client's paint and its present -- and
     * a mod must see them paired: one end for each begin, never an end on its own. The client
     * presents more often than it paints a frame, from its loading screens and from a paint that
     * found nothing to draw into, so the pairing is kept here rather than trusted to the client.
     *
     * @return false when a frame is already open, and nothing is filled in.
     */
    bool Begin(wxl_frame_args& out);

    /**
     * @brief Closes the open frame, and fills in what FrameEnd carries: the same index and delta
     *        its FrameBegin did.
     * @return false when no frame is open, and nothing is filled in.
     */
    bool End(wxl_frame_args& out);

    /**
     * @brief The open frame's index and delta, for the slots inside it: the world pass and the
     *        end of the interface carry the frame they are part of.
     * @return false when no frame is open, and nothing is filled in. A slot outside a frame is
     *         one the client reached from somewhere no frame was opened, and is not announced.
     */
    bool Current(wxl_frame_args& out);

    /// Forgets any open frame, for a test or a restart. The index keeps counting.
    void Reset();
}
