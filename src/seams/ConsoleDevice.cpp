// The ConsoleDevice seam: what the runtime does once the graphics device is brought up.
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

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Stages.hpp"

namespace wxl::seam::ConsoleDevice::Initialize
{
    void WXL_CALL Before(const char*, bool, bool) {}

    void WXL_CALL After(const char*, bool, bool)
    {
        // The function returns only with a device -- each failure ends the process -- but the
        // stage means a device exists, so that is what is asked rather than assumed.
        if (wxl::core::binding::gx_device_exists()) wxl::core::stages::Reach(WXL_STAGE_GRAPHICS);
    }
}
