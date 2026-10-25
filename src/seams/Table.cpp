// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
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

    namespace
    {
        struct X64OnlyRow
        {
            const char* name;
            const char* why;
        };

        constexpr X64OnlyRow kX64Only[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_X64_ONLY(domain, name, why) {#domain "." #name, why},
#include "wxl/seams/seams.def"
#undef WXL_SEAM_X64_ONLY
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
            {nullptr, nullptr},
        };

        struct RaisesRow
        {
            const char* seam;
            wxl_event   event;
        };
    }

    const char* X64Only(const char* name)
    {
        if (!name) return nullptr;
        for (const X64OnlyRow& row : kX64Only)
            if (row.name && strcmp(row.name, name) == 0) return row.why;
        return nullptr;
    }

    std::vector<wxl_event> RaisedEvents(bool stock)
    {
        constexpr RaisesRow kRaised[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_RAISES(domain, name, event) {#domain "." #name, WXL_EV_##event},
#include "wxl/seams/seams.def"
#undef WXL_SEAM_RAISES
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
        };

        std::vector<wxl_event> events;
        for (const RaisesRow& row : kRaised)
        {
            if (stock && X64Only(row.seam)) continue;
            if (std::find(events.begin(), events.end(), row.event) == events.end()) events.push_back(row.event);
        }
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
