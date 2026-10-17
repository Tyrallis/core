// The x86 attachment: MinHook detours at resolved addresses, and the services built on them.
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

#include "runtime/Attachment.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "runtime/Core.hpp"
#include "runtime/Image.hpp"
#include "runtime/WorldSeam.hpp"
#include "services/HookService.hpp"
#include "services/PatchService.hpp"

#include "wxl/services/hook.h"
#include "wxl/services/patch.h"

namespace wxl::core::attachment
{
    void Prepare()
    {
        if (!wxl::hook::Init())
            WLOG_ERROR(kTag, "hook engine unavailable; detours will be refused");
    }

    std::vector<wxl_event> Events()
    {
        return { WXL_EV_WorldEnter };
    }

    std::vector<wxl::host::PublishedService> Services()
    {
        return {
            { WXL_HOOK_SERVICE, WXL_HOOK_SERVICE_VERSION, &services::HookApi() },
            { WXL_HOOK_RAW_SERVICE, WXL_HOOK_RAW_SERVICE_VERSION, &services::HookRawApi() },
            { WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION, &services::PatchApi() },
        };
    }

    void Install()
    {
        // The core's own detours go in before any mod can attach behind them, and only in the
        // image whose addresses they are.
        if (!image::IsClient())
            WLOG_WARN(kTag, "the executable is not Wow.exe build %u: named hook points and "
                            "WorldEnter are unavailable", static_cast<unsigned>(image::kBuild));
        else if (!world::Install())
            WLOG_ERROR(kTag, "world seam not installed; WorldEnter will not be raised");
    }
}
