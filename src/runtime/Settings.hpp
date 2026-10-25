// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// WarcraftXL.ini, next to the client executable: what the player sets without an environment
// variable.

#pragma once

#include "common/Log.hpp"

#include <cstddef>
#include <cstdint>

namespace wxl::core::settings
{
    /// The file's name, next to the client executable.
    constexpr wchar_t kFileName[] = L"WarcraftXL.ini";

    /// What the file says. Every member has the value the runtime uses without the file.
    struct Settings
    {
        bool            found     = false; ///< the file exists and was read
        bool            has_level = false; ///< [log] level was given
        wxl::log::Level level     = wxl::log::Level::Info;
        unsigned        keep      = 30;    ///< earlier launches' logs kept beside the current one

        struct Tag
        {
            char            name[64];
            wxl::log::Level level;
        };
        Tag      tags[32] = {};
        unsigned tag_count = 0;

        /// Lines of the file that were not understood, as messages for the log, at most 8.
        char     problems[8][160] = {};
        unsigned problem_count    = 0;
    };

    /**
     * @brief Reads settings from the text of an ini file.
     *
     * One section is read, `[log]`:
     *
     *     [log]
     *     level = debug          ; trace, debug, info, warn, error or off
     *     keep = 30              ; earlier launches kept in Logs\, 0 for none
     *     level.wxl.probe = info ; a level for one tag: a mod's id, "host", "core"...
     *
     * `;` and `#` start a comment. Other sections are left for later uses; an unknown key or a
     * value that does not read is noted in `problems` and otherwise ignored.
     */
    void Parse(const char* text, size_t size, Settings& out);

    /// Reads @p path (kernel32 only, safe under the loader lock); `found` is false without it.
    void Load(const wchar_t* path, Settings& out);

    /**
     * @brief Hands the levels to the log: the file's level unless WXL_LOG_LEVEL is set, which
     *        wins, and every tag's own.
     */
    void ApplyToLog(const Settings& settings);
}
