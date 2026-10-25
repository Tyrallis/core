// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.patch service: checked, journaled byte writes into the client's image.

#pragma once

#include "wxl/services/patch.h"

namespace wxl::core::services
{
    /// The table, WXL_PATCH_SERVICE version WXL_PATCH_SERVICE_VERSION.
    const wxl_patch_api& PatchApi();
}
