// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The x86 realisation of the seam table: one detour per row, at the row's resolved address.

#pragma once

namespace wxl::core::seams
{
    /**
     * @brief Attaches every seam's detour (Before, the original, After) at the address WXL_SITE
     *        resolved for the row's symbol.
     *
     * Call once the hook engine is up and only in the client image; each detour goes in before
     * any mod can attach to the same point, so the runtime's own seam runs closest to the caller.
     * @return true when every seam is live; a failed one is logged by name.
     */
    bool Install();
}
