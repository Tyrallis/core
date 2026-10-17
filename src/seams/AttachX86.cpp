// The x86 realisation of the seam table: the detours generated from seams.def.
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

#include "seams/AttachX86.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "runtime/Core.hpp"
#include "seams/Detour.hpp"

#include "wxl/offsets/site.hpp"
#include "wxl/seams/seams.h"

#include <cstddef>
#include <cstdint>

namespace
{
    // One Link per row: the detour in the row's convention, with the row's Before and After.
#define WXL_SEAM(domain, name, symbol, ret, conv, params)                                  \
    using domain##_##name = ::wxl::core::seams::DetourOf<::wxl::seam::detail::conv_##conv,     \
                                                     ret params>::Link<                    \
        &::wxl::seam::domain::name::Before, &::wxl::seam::domain::name::After>;
#include "wxl/seams/seams.def"
#undef WXL_SEAM

    struct Row
    {
        const char* name;    ///< the point's name, "<domain>.<name>", as the log and wxl.hook say it
        uintptr_t   address; ///< the function's address in Wow.exe build 12340
        void*       detour;
        void**      original;
    };

    const Row kRows[] = {
#define WXL_SEAM(domain, name, symbol, ret, conv, params)                                       \
    { #domain "." #name, WXL_SITE(symbol, 0), reinterpret_cast<void*>(&domain##_##name::Detour), \
      reinterpret_cast<void**>(&domain##_##name::original) },
#include "wxl/seams/seams.def"
#undef WXL_SEAM
    };
}

namespace wxl::core::seams
{
    bool Install()
    {
        bool all = true;
        for (const Row& row : kRows)
        {
            const wxl::hook::Id id = wxl::hook::Attach(
                row.name, reinterpret_cast<void*>(row.address), row.detour, row.original);
            if (id == wxl::hook::kNone)
            {
                WLOG_ERROR(kTag, "seam %s not attached at 0x%08zX", row.name,
                           static_cast<size_t>(row.address));
                all = false;
                continue;
            }
            WLOG_INFO(kTag, "seam %s attached at 0x%08zX", row.name,
                      static_cast<size_t>(row.address));
        }
        return all;
    }
}
