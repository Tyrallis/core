// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The core runtime: where its files live, and the mod host it stands up for the client.

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
     * @brief Stands up the runtime: the attachment to the client (see Attachment.hpp), then
     *        the mod host, which discovers, admits and loads every mod under Mods\ next to the
     *        client executable and reports WXL_STAGE_EARLY.
     *
     * Runs once, on whichever thread calls it first: on x86 the client's main thread from the
     * entry trampoline, or the core's own thread when the executable has none; on x64 the
     * client's call to WarcraftXLInit. Must not run under the loader lock, since loading a mod
     * is a LoadLibrary. The host lives for the rest of the process; the mods are told
     * WXL_STAGE_SHUTDOWN and unloaded by the Client.Destroy seam. Never throws; a failure is
     * logged and the client goes on without mods.
     * @return true when the host is up and its mods are loaded. A second call reports the first.
     */
    bool Start();

    /// The live mod host, or null before Start or after a failed one.
    wxl::host::ModHost* Host();
}
