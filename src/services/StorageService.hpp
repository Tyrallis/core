// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.storage service: the client's files, over the binding, the same on both builds.

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
