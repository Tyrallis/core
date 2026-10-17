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

#include <cstring>

namespace wxl::core::seams
{
    namespace
    {
        constexpr Seam kSeams[] = {
#define WXL_SEAM(domain, name, symbol, params) \
    { #domain "." #name, symbol, "void __cdecl " #params },
#include "wxl/seams/seams.def"
#undef WXL_SEAM
        };
    }

    std::span<const Seam> Table() { return kSeams; }

    const Seam* Find(const char* name)
    {
        if (!name) return nullptr;
        for (const Seam& seam : kSeams)
            if (strcmp(seam.name, name) == 0) return &seam;
        return nullptr;
    }
}
