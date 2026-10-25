// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// One launch of the client, as the log tells it: the header that says what ran where, the mods,
// and the line that says it ended.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::host
{
    class ModHost;
}

namespace wxl::core::settings
{
    struct Settings;
}

namespace wxl::core::session
{
    /// The tag of the launch's own lines.
    constexpr const char* kTag = "session";

    /**
     * @brief Reads WarcraftXL.ini next to the executable, opens Logs\WarcraftXL.log for this
     *        launch (keeping the previous ones) and writes the header: when, which runtime and
     *        contract, the commits it was built from, the executable, Windows and Wine, the
     *        processor, the memory and the address space, the command line, the settings.
     *
     * Runs in DllMain: kernel32 and ntdll only. Once; a second call does nothing.
     * @return true when the log is open.
     */
    bool Begin();

    /**
     * @brief Writes what the host found in Mods\: one line per mod loaded (version, the abi its
     *        manifest asks for and the one its binary was built with, its binary) or not loaded
     *        (why), after a count.
     */
    void ReportMods(const wxl::host::ModHost& host);

    /**
     * @brief Writes "session end: <how>, after <uptime>" and flushes, once: the line a crashed
     *        launch lacks.
     */
    void End(const char* how);

    /// WarcraftXL.ini as @ref Begin read it; the defaults before, or without the file.
    const wxl::core::settings::Settings& CurrentSettings();

    /// Milliseconds since @ref Begin.
    uint64_t UptimeMs();

    /// "1h 02m 03s", "4m 10s", "12.3 s".
    void FormatDuration(uint64_t ms, char* out, size_t capacity);
}
