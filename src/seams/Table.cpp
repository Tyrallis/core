// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The seam table as data, expanded from seams.def.

#include "seams/Table.hpp"

#include <algorithm>
#include <cstring>

namespace wxl::core::seams
{
    namespace
    {
        constexpr Seam kSeams[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params) \
    { #domain "." #name, symbol, #ret, #conv, #params, false },
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params) \
    { #domain "." #name, symbol, #ret, #conv, #params, true },
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM
        };
    }

    std::span<const Seam> Table() { return kSeams; }

    std::vector<wxl_event> RaisedEvents()
    {
        constexpr wxl_event kRaised[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_RAISES(domain, name, event) WXL_EV_##event,
#include "wxl/seams/seams.def"
#undef WXL_SEAM_RAISES
#undef WXL_SEAM_GATE
#undef WXL_SEAM
        };

        std::vector<wxl_event> events;
        for (const wxl_event event : kRaised)
            if (std::find(events.begin(), events.end(), event) == events.end())
                events.push_back(event);
        return events;
    }

    const Seam* Find(const char* name)
    {
        if (!name) return nullptr;
        for (const Seam& seam : kSeams)
            if (strcmp(seam.name, name) == 0) return &seam;
        return nullptr;
    }
}
