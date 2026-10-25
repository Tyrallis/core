// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The M2 seams: the model batches the client draws (ModelBatchDrawn), and the bones of the
// models wxl.m2 hung to follow their parent's.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"
#include "services/M2Service.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

namespace wxl::seam::M2::DrawBatch
{
    void WXL_CALL After(void* self)
    {
        // Thousands of batches a frame: nothing at all is done while no mod listens.
        wxl::host::ModHost* host = wxl::core::Host();
        if (!self || !host || !host->Listening(WXL_EV_ModelBatchDrawn)) return;

        wxl_model_batch_args args{};
        if (!wxl::core::binding::gx_model_draw(self, &args)) return;
        host->Raise(WXL_EV_ModelBatchDrawn, &args);
    }
}

namespace wxl::seam::M2::AnimateMT
{
    void WXL_CALL After(void* self, const void*, const void*, const void*, float, float)
    {
        wxl::core::services::m2::PoseAttached(self);
    }
}

namespace wxl::seam::M2::AnimateMTSimple
{
    void WXL_CALL After(void* self, const void*, const void*, const void*, float, float)
    {
        wxl::core::services::m2::PoseAttached(self);
    }
}
