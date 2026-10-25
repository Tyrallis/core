// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The seam table as data: what the runtime can say about its seams without reaching the client.

#pragma once

#include "wxl/events.h"

#include <span>
#include <vector>

namespace wxl::core::seams
{
    /// Which entry points a row declares: its shape in seams.def.
    enum class Shape
    {
        Observe, ///< WXL_SEAM: Before and After
        Before,  ///< WXL_SEAM_BEFORE: Before only
        After,   ///< WXL_SEAM_AFTER: After only
        Gate,    ///< WXL_SEAM_GATE: Gate, which can take the call
    };

    /// One row of seams.def, without its address: the same on both builds.
    struct Seam
    {
        const char* name;       ///< "<domain>.<name>": the point's name
        const char* symbol;     ///< the client function, as the recompiled client spells it
        const char* returns;    ///< the row's return type, as text; "void" for none
        const char* convention; ///< the row's x86 calling convention: cdecl, thiscall or stdcall
        const char* params;     ///< the row's parameter list, as text, parentheses included
        Shape       shape;      ///< the entry points the row declares
    };

    /// Every seam, in table order.
    std::span<const Seam> Table();

    /// The seam carrying a point name, or null.
    const Seam* Find(const char* name);

    /// Why the stock client cannot take a seam (its WXL_SEAM_X64_ONLY row), or null.
    const char* X64Only(const char* name);

    /**
     * @brief Every event some seam's body raises, each once, in table order.
     *
     * Derived from the WXL_SEAM_RAISES rows, so both attachments answer Events() from the same
     * table. For the stock client (`stock`), an event only WXL_SEAM_X64_ONLY seams raise is left
     * out: that runtime attaches no detour for them.
     */
    std::vector<wxl_event> RaisedEvents(bool stock = false);
}
