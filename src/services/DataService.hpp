// The wxl.data service: the client's databases, served from a mod's files.
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

#include <cstddef>

#include "wxl/services/data.h"

namespace wxl::core::services
{
    /// The table, WXL_DATA_SERVICE version WXL_DATA_SERVICE_VERSION.
    const wxl_data_api& DataApi();

    namespace data
    {
        /**
         * @brief What the client should open instead, for a path it is about to open.
         *
         * Called by the Storage seam at every open. Answers null for anything that is not a
         * client database and for one nobody serves, which is almost every call: the check is
         * a suffix test and a lookup by name.
         *
         * Every database it sees is remembered as read, which is what lets a mod be told that
         * an override it registered has come too late to be used.
         */
        const char* ServedPath(const char* path, std::size_t size);

        /// Gives back every table a mod was serving, when it is disabled or unloaded.
        void ForgetMod(uint32_t mod);

        /// Forgets everything, on the way out.
        void Clear();
    }
}
