// The wxl.hook and wxl.hook.raw services: the SDK's hook tables over the engine.
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

#include "wxl/services/hook.h"

namespace wxl::core::services
{
    /// The by-name table, WXL_HOOK_SERVICE version WXL_HOOK_SERVICE_VERSION.
    const wxl_hook_api& HookApi();

    /// The by-address table, WXL_HOOK_RAW_SERVICE version WXL_HOOK_RAW_SERVICE_VERSION.
    const wxl_hook_raw_api& HookRawApi();
}
