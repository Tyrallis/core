// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.action service: what the player can do, through the client's own functions, the same
// on both builds over the binding's action rows.

#pragma once

#include "wxl/services/action.h"

namespace wxl::core::services
{
    /// The table, WXL_ACTION_SERVICE version WXL_ACTION_SERVICE_VERSION.
    const wxl_action_api& ActionApi();
}
