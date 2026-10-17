// The x86 realisation of the seam table: one detour per row, at the row's resolved address.
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
