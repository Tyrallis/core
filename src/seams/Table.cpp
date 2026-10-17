// The seam table as data, expanded from seams.def.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include "seams/Table.hpp"

#include <algorithm>
#include <cstring>

namespace wxl::core::seams
{
    namespace
    {
        constexpr Seam kSeams[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params) \
    { #domain "." #name, symbol, #ret, #conv, #params },
#include "wxl/seams/seams.def"
#undef WXL_SEAM
        };
    }

    std::span<const Seam> Table() { return kSeams; }

    std::vector<wxl_event> RaisedEvents()
    {
        constexpr wxl_event kRaised[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)
#define WXL_SEAM_RAISES(domain, name, event) WXL_EV_##event,
#include "wxl/seams/seams.def"
#undef WXL_SEAM_RAISES
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
