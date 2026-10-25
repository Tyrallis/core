// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Profiler zones for the recompiled client's own source. WarcraftXL.dll carries the process's one
// profiler client (Tracy, on demand); the client marks its main loop's hot spots through the two
// functions below, one line each:
//
//     void CGWorldFrame::OnWorldUpdate() {
//         WXL_CORE_ZONE("World.Update");
//         ...
//
// A zone lasts until the end of its scope. Nothing is recorded while no viewer is connected:
// the zone costs two calls that return at once. In a runtime built with WXL_PROFILE off, and in
// any build that does not define WXL_CORE_PROFILE (the Win32 layout build), the macro is empty.

#ifndef WXL_CORE_PROFILE_H
#define WXL_CORE_PROFILE_H

#include <stdint.h>

#include "wxl/core/init.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A place in the client's source, laid out as the profiler reads it: the zone's name, the
 *        function and file it stands in, its line and colour (0xRRGGBB, 0 for the viewer's).
 *
 * Read by the profiler for the process's life: a static of the client.
 */
typedef struct wxl_core_zone_site
{
    const char* name;
    const char* function;
    const char* file;
    uint32_t    line;
    uint32_t    color;
} wxl_core_zone_site;

/// Opens a zone at @p site on the calling thread; 0 when nothing is recorded.
WXL_CORE_API uint64_t WXL_CALL WarcraftXLZoneBegin(const wxl_core_zone_site* site);

/// Closes what WarcraftXLZoneBegin answered, on the same thread, innermost first.
WXL_CORE_API void WXL_CALL WarcraftXLZoneEnd(uint64_t zone);

#ifdef __cplusplus
} // extern "C"

namespace wxl::core
{
    /// A zone from construction to the end of the scope.
    class ClientZone
    {
    public:
        explicit ClientZone(const wxl_core_zone_site* site) : zone_(WarcraftXLZoneBegin(site)) {}
        ~ClientZone()
        {
            if (zone_) WarcraftXLZoneEnd(zone_);
        }
        ClientZone(const ClientZone&)            = delete;
        ClientZone& operator=(const ClientZone&) = delete;

    private:
        uint64_t zone_;
    };
} // namespace wxl::core

#define WXL_CORE_ZONE_CONCAT_(a, b) a##b
#define WXL_CORE_ZONE_CONCAT(a, b)  WXL_CORE_ZONE_CONCAT_(a, b)

#if defined(WXL_CORE_PROFILE) && WXL_CORE_PROFILE
#define WXL_CORE_ZONE(name)                                                                         \
    static const ::wxl_core_zone_site WXL_CORE_ZONE_CONCAT(wxl_core_zone_site_, __LINE__){          \
        name, __func__, __FILE__, static_cast<uint32_t>(__LINE__), 0};                              \
    const ::wxl::core::ClientZone WXL_CORE_ZONE_CONCAT(wxl_core_zone_, __LINE__)(                   \
        &WXL_CORE_ZONE_CONCAT(wxl_core_zone_site_, __LINE__))
#else
#define WXL_CORE_ZONE(name) static_cast<void>(0)
#endif

#endif // __cplusplus

#endif // WXL_CORE_PROFILE_H
