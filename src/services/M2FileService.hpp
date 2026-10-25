// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The wxl.m2file service: reshaping a model file the client has parsed. The loader's stages
// reach mods as the events ModelParsed, SkinProfileParsed and ModelSequenceLoad (seams/M2Shared.cpp).

#pragma once

#include <cstdint>

#include "wxl/services/m2file.h"

namespace wxl::core::services
{
    /// The table, WXL_M2FILE_SERVICE version WXL_M2FILE_SERVICE_VERSION.
    const wxl_m2file_api& M2FileApi();

    namespace m2file
    {
        /// Whether the client keeps a parsed array's elements as an offset from the array (the
        /// recompiled client) rather than as their address (the stock client).
        constexpr bool kSelfRelative = sizeof(void*) == 8;
    }
}
