// The wxl.storage service: the client's files, over the binding, the same on both builds.
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

#include "wxl/services/storage.h"

namespace wxl::core::services
{
    /**
     * @brief The table, WXL_STORAGE_SERVICE version WXL_STORAGE_SERVICE_VERSION.
     *
     * Its struct_size stops short of the rows this runtime cannot answer: a mod reading it
     * through the SDK's facade is told they are unsupported rather than calling into nothing.
     */
    const wxl_storage_api& StorageApi();

    namespace storage
    {
        /// Closes every file a mod left open and forgets its handles. Called on the way out.
        void CloseEverything();

        /// The same for one mod, when it is disabled or unloaded.
        void CloseFilesOf(uint32_t mod);
    }
}
