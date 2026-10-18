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
// The columns are stringized by each shape, before a column that names a macro is expanded.
#define WXL_SEAM_ROW(point, symbol, ret, conv, params, shape) \
    { point, symbol, ret, conv, params, Shape::shape },
#define WXL_SEAM(domain, name, symbol, ret, conv, params) \
    WXL_SEAM_ROW(#domain "." #name, symbol, #ret, #conv, #params, Observe)
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params) \
    WXL_SEAM_ROW(#domain "." #name, symbol, #ret, #conv, #params, Before)
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params) \
    WXL_SEAM_ROW(#domain "." #name, symbol, #ret, #conv, #params, After)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params) \
    WXL_SEAM_ROW(#domain "." #name, symbol, #ret, #conv, #params, Gate)
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
#undef WXL_SEAM_ROW
        };
    }

    std::span<const Seam> Table() { return kSeams; }

    std::vector<wxl_event> RaisedEvents()
    {
        constexpr wxl_event kRaised[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_RAISES(domain, name, event) WXL_EV_##event,
#include "wxl/seams/seams.def"
#undef WXL_SEAM_RAISES
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
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
