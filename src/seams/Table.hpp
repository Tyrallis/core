// The seam table as data: what the runtime can say about its seams without reaching the client.
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

#pragma once

#include <span>

namespace wxl::core::seams
{
    /// One row of seams.def, without its address: the same on both builds.
    struct Seam
    {
        const char* name;      ///< "<domain>.<name>": the point's name
        const char* symbol;    ///< the client function, as the recompiled client spells it
        const char* signature; ///< the C signature of Before, After and the x86 detour, as text
    };

    /// Every seam, in table order.
    std::span<const Seam> Table();

    /// The seam carrying a point name, or null.
    const Seam* Find(const char* name);
}
