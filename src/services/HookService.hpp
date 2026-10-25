// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.hook and wxl.hook.raw services: the SDK's hook tables over the engine.

#pragma once

#include "wxl/services/hook.h"

namespace wxl::core::services
{
    /// The by-name table, WXL_HOOK_SERVICE version WXL_HOOK_SERVICE_VERSION.
    const wxl_hook_api& HookApi();

    /// The by-address table, WXL_HOOK_RAW_SERVICE version WXL_HOOK_RAW_SERVICE_VERSION.
    const wxl_hook_raw_api& HookRawApi();

    /**
     * @brief Detaches every hook a mod attached and has not detached itself.
     *
     * Attaching is recorded against whichever mod the host says is running, which is the mod
     * that asked. What it leaves behind is taken away before its binary is freed: see
     * attachment::Release, which is what calls this.
     */
    void ReleaseHooksOf(uint32_t mod);

    /// The same for every mod, on the way out.
    void ReleaseEveryHook();
}
