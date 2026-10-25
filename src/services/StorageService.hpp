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

        /**
         * @brief Where an open of @p name is answered from a folder of the search path.
         *
         * True when a folder holds the name and wins it: a folder of a priority above 0 over
         * the client's own data, one of 0 or below only when the client has nothing by that name.
         * @p out then receives the name the client resolves to that file -- its path relative
         * to the client's folder -- and @p flags the bit that makes the client look among its
         * loose files for it. False, nothing written, otherwise.
         */
        bool Resolve(const char* name, char* out, size_t capacity, uint32_t* flags);

        /// Whether a name resolves, in the client's archives or in a folder of the search path.
        bool Exists(const char* name);

        /// Whether this thread is listing the archives for find: the opens of their listfiles
        /// are the runtime's own, not the client's.
        bool Listing();

        /// Forgets the search path. Called on the way out, and by the harness.
        void ForgetSearchPaths();
    }
}
