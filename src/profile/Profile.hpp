// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The process's profiler: Tracy's client, compiled into WarcraftXL.dll on demand. Nothing is
// sent, and almost nothing done, until a viewer connects to port 8086 (TRACY_PORT to change it).
// Three ways in, all through here:
//
//   the host       a zone per event dispatch and per handler (wxl/host/profile.hpp)
//   wxl.profile    a mod's own zones, graphs, messages and frame sets
//   the client     WarcraftXLZoneBegin/End, the fork's main-loop zones on x64 (wxl/core/profile.h)
//
// The runtime marks a frame at every present, and while a viewer is connected also plots the
// footprint counters when accounting runs. Built without WXL_PROFILE, every function here is an
// empty inline and the service is not published.

#pragma once

#include <cstdint>
#include <string_view>

#include "wxl/services/profile.h"

namespace wxl::host
{
    class ModHost;
    struct Profiler;
}

namespace wxl::core::profile
{
#if defined(WXL_CORE_PROFILE) && WXL_CORE_PROFILE
    /**
     * @brief Starts the profiler on a thread of its own, unless WXL_PROFILE=0 is in the
     *        environment: it calibrates its clock for a fifth of a second, which the client's
     *        start does not wait for. Zones before it is up are not recorded. Once.
     */
    void Start();

    /// The host's zones go through this from now on. Before the mods load.
    void Attach(wxl::host::ModHost& host);

    /// At every present, on the main thread: ends a frame, tells the host whether a viewer is
    /// watching, plots the footprint counters while one is.
    void Present();

    /// Whether the profiler is up and a viewer connected.
    bool Connected();

    /// Whether the profiler is up, connected or not.
    bool Started();

    /// A place zones are named after, copied and kept for the process's life; the same values
    /// answer the same place. Null for an empty name.
    const void* Location(std::string_view name, std::string_view function, std::string_view file,
                         uint32_t line, uint32_t color);

    /// A zone at a location on the calling thread: 0 when nothing is recorded.
    uint64_t Begin(const void* location);
    void     End(uint64_t zone);

    /// The table published as WXL_PROFILE_SERVICE.
    const wxl_profile_api& Api();
#else
    inline void Start() {}
    inline void Attach(wxl::host::ModHost&) {}
    inline void Present() {}
    inline bool Connected() { return false; }
#endif
} // namespace wxl::core::profile
