// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.data service: the client's databases, served from a mod's files.

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
         * @brief Whether the client should open something else, for a path it is about to open.
         *
         * Called by the Storage seam at every open, which is why the database suffix is read
         * off the path before anything else happens: for the overwhelming majority of opens
         * the answer costs four character comparisons and no allocation, no lock and no lookup.
         *
         * When a mod serves the table, what it serves is written to @p out, terminated there,
         * and true returned. It is copied rather than handed over because the caller's buffer
         * outlives the call and the service's own strings do not.
         *
         * Every database it sees is remembered as read, which is what lets a mod be told that
         * an override it registered has come too late to be used.
         */
        bool ServedPath(const char* path, std::size_t size, char* out, std::size_t capacity);

        /// Gives back every table a mod was serving, when it is disabled or unloaded.
        void ForgetMod(uint32_t mod);

        /// Forgets everything, on the way out.
        void Clear();
    }
}
