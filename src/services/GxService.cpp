// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The wxl.gx service: the client's D3D9 device. The batches it draws reach mods as the event
// ModelBatchDrawn (seams/M2.cpp).

#include "services/GxService.hpp"

#include "binding/Binding.hpp"

namespace
{
    void* WXL_CALL D3d9Device(void) { return wxl::core::binding::gx_d3d9_device(); }

    void WXL_CALL InvalidateState(void) { wxl::core::binding::gx_invalidate_state(); }

    const wxl_gx_api kApi = {sizeof(wxl_gx_api), &D3d9Device, &InvalidateState};
}

namespace wxl::core::services
{
    const wxl_gx_api& GxApi() { return kApi; }
}
