// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.hook and wxl.hook.raw services: the SDK's hook tables over the engine.

#include "services/HookService.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "hook/Points.hpp"
#include "runtime/Core.hpp"
#include "runtime/Image.hpp"

#include "wxl/host/mod_host.hpp"

#include <windows.h>
#include <cstring>
#include <vector>

namespace
{
    constexpr char kTag[] = "hook";

    wxl_str Str(const char* text)
    {
        return wxl_str{text, static_cast<uint32_t>(strlen(text))};
    }

    /// One hook a mod attached, and the mod it belongs to. A detour is a jump into that
    /// mod's binary, so the runtime has to know whose it is before the binary can be freed.
    struct Owned
    {
        wxl_hook id;
        uint32_t mod; ///< the host's token for the mod, 0 when the runtime attached it itself
    };

    std::vector<Owned> g_owned;

    /// The mod the host says is running: whoever is inside the call that is attaching.
    uint32_t Asking()
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0u;
    }

    /// Records a successful attach against the mod that asked for it.
    wxl_hook Remember(wxl_hook hook)
    {
        if (hook != WXL_HOOK_NONE) g_owned.push_back(Owned{hook, Asking()});
        return hook;
    }

    void Forget(wxl_hook hook)
    {
        for (std::size_t i = 0; i < g_owned.size(); ++i)
        {
            if (g_owned[i].id != hook) continue;
            g_owned.erase(g_owned.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
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
        return Remember(wxl::hook::Attach(point->name, reinterpret_cast<void*>(point->address),
                                          detour, original, priority));
    }

    wxl_result WXL_CALL Detach(wxl_hook hook)
    {
        if (!wxl::hook::Detach(hook)) return WXL_E_NOT_FOUND;
        Forget(hook);
        return WXL_OK;
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
        return Remember(wxl::hook::Attach(name ? name : "?", reinterpret_cast<void*>(target),
                                          detour, original, priority));
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

    void ReleaseHooksOf(uint32_t mod)
    {
        std::size_t released = 0;
        for (std::size_t i = g_owned.size(); i > 0; --i)
        {
            Owned& owned = g_owned[i - 1];
            if (owned.mod != mod) continue;
            wxl::hook::Detach(owned.id);
            g_owned.erase(g_owned.begin() + static_cast<std::ptrdiff_t>(i - 1));
            ++released;
        }
        if (released)
            WLOG_INFO(kTag, "detached %zu hook(s) a mod left attached", released);
    }

    void ReleaseEveryHook()
    {
        for (const Owned& owned : g_owned) wxl::hook::Detach(owned.id);
        if (!g_owned.empty())
            WLOG_INFO(kTag, "detached %zu hook(s) still attached on the way out", g_owned.size());
        g_owned.clear();
    }
}
