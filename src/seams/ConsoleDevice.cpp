// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The ConsoleDevice seam: what the runtime does once the graphics device is brought up.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Stages.hpp"

namespace wxl::seam::ConsoleDevice::Initialize
{
    void WXL_CALL After(const char*, bool, bool)
    {
        // The function returns only with a device -- each failure ends the process -- but the
        // stage means a device exists, so that is what is asked rather than assumed.
        if (wxl::core::binding::gx_device_exists()) wxl::core::stages::Reach(WXL_STAGE_GRAPHICS);
    }
}
