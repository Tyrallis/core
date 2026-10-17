// The x64 attachment's own entry: what WarcraftXLInit hands it before the host comes up.
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

#include "wxl/core/init.h"

namespace wxl::core::attachment
{
    /**
     * @brief Keeps the accessors the client passed to WarcraftXLInit.
     *
     * Reads only what init->struct_size covers; a field the client's header did not have yet
     * stays null and the accessor built on it answers its documented default.
     */
    void Bind(const wxl_core_init& init);
}
