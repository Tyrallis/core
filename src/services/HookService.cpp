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

#include "services/HookService.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "hook/Points.hpp"
#include "runtime/Image.hpp"

#include <windows.h>
#include <cstring>

namespace
{
    constexpr char kTag[] = "hook";

    wxl_str Str(const char* text)
    {
        return wxl_str{text, static_cast<uint32_t>(strlen(text))};
    }

    /// The named points, or none at all in an image whose addresses they do not describe.
    std::span<const wxl::hook::Point> NamedPoints()
    {
        return wxl::core::image::IsClient() ? wxl::hook::Points()
                                            : std::span<const wxl::hook::Point>();
    }

    // --- wxl.hook ---------------------------------------------------------------------------

    uint32_t WXL_CALL Points(wxl_hook_point* out, uint32_t capacity)
    {
        const std::span<const wxl::hook::Point> points = NamedPoints();
        if (out)
        {
            uint32_t i = 0;
            for (const wxl::hook::Point& point : points)
            {
                if (i == capacity) break;
                out[i].struct_size = sizeof(wxl_hook_point);
                out[i].name        = Str(point.name);
                out[i].signature   = Str(point.signature);
                ++i;
            }
        }
        return static_cast<uint32_t>(points.size());
    }

    wxl_bool WXL_CALL HasPoint(const char* name)
    {
        return wxl::core::image::IsClient() && wxl::hook::FindPoint(name) ? WXL_TRUE : WXL_FALSE;
    }

    wxl_hook WXL_CALL Attach(const char* name, void* detour, void** original, int32_t priority)
    {
        const wxl::hook::Point* point = wxl::core::image::IsClient() ? wxl::hook::FindPoint(name)
                                                                     : nullptr;
        if (!point)
        {
            WLOG_WARN(kTag, "attach '%s': no point of that name", name ? name : "(null)");
            return WXL_HOOK_NONE;
        }
        return wxl::hook::Attach(point->name, reinterpret_cast<void*>(point->address), detour,
                                 original, priority);
    }

    wxl_result WXL_CALL Detach(wxl_hook hook)
    {
        return wxl::hook::Detach(hook) ? WXL_OK : WXL_E_NOT_FOUND;
    }

    // --- wxl.hook.raw -----------------------------------------------------------------------

    uintptr_t WXL_CALL ImageBase(void)
    {
        return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    }

    uint32_t WXL_CALL ImageBuild(void) { return wxl::core::image::Build(); }

    wxl_hook WXL_CALL AttachAt(const char* name, uintptr_t target, void* detour, void** original,
                               int32_t priority)
    {
        return wxl::hook::Attach(name ? name : "?", reinterpret_cast<void*>(target), detour,
                                 original, priority);
    }

    const wxl_hook_api g_hook = {
        sizeof(wxl_hook_api),
        &Points,
        &HasPoint,
        &Attach,
        &Detach,
    };

    const wxl_hook_raw_api g_hookRaw = {
        sizeof(wxl_hook_raw_api),
        &ImageBase,
        &ImageBuild,
        &AttachAt,
        &Detach,
    };
}

namespace wxl::core::services
{
    const wxl_hook_api&     HookApi() { return g_hook; }
    const wxl_hook_raw_api& HookRawApi() { return g_hookRaw; }
}
