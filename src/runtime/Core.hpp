// The core runtime: where its files live, and the mod host it stands up for the client.
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

#pragma once

#include <cstdint>

namespace wxl::host
{
    class ModHost;
}

namespace wxl::core
{
    /// The tag the core logs its own lines under.
    constexpr const char* kTag = "core";

    /// The runtime identity handed to mods through wxl_runtime_info.
    constexpr const char* kRuntimeId   = "core";
    constexpr const char* kRuntimeName = "WarcraftXL core";

    /**
     * @brief Opens the log at Logs\WarcraftXL.log next to the client executable.
     *
     * Safe under the loader lock: it calls kernel32 only (no CRT file I/O, no C++ filesystem).
     * @return true when the file is open.
     */
    bool OpenLog();

    /**
     * @brief Whether the client executable carries the patcher's entry-point trampoline, which
     *        calls WarcraftXLEntry before the client's own entry point.
     *
     * Reads the executable's headers in memory: safe under the loader lock.
     */
    bool HasEntryTrampoline();

    /**
     * @brief Stands up the runtime: the hook engine, the services, the core's own detours, then
     *        the mod host, which discovers, admits and loads every mod under Mods\ next to the
     *        client executable and reports WXL_STAGE_EARLY.
     *
     * Runs once, on whichever thread calls it first: the client's main thread from the entry
     * trampoline, or the core's own thread when the executable has none. Must not run under
     * the loader lock, since loading a mod is a LoadLibrary. The host lives for the rest of
     * the process: no seam exists yet from which a shutdown stage could run before the client
     * tears its DLLs down, so nothing is unloaded. Never throws; a failure is logged and the
     * client goes on without mods.
     */
    void Start();

    /// The live mod host, or null before Start or after a failed one.
    wxl::host::ModHost* Host();
}
