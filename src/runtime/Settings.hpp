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

        bool     has_profile         = false; ///< a [profile] section was there
        bool     has_profile_enabled = false; ///< [profile] enabled was given
        bool     profile_enabled     = false;
        uint16_t profile_port        = 8086;
        bool     profile_any         = false; ///< listen = any: every interface, not 127.0.0.1 only

        /// Lines of the file that were not understood, as messages for the log, at most 8.
        char     problems[8][160] = {};
        unsigned problem_count    = 0;
    };

    /// Whether the profiler starts, where it listens, and what decided it.
    struct ProfileChoice
    {
        bool        enabled  = false;
        bool        from_env = false;   ///< WXL_PROFILE in the environment decided, over the file
        uint16_t    port     = 8086;
        bool        any      = false;   ///< every interface; otherwise 127.0.0.1 only
        const char* ignored  = nullptr; ///< a WXL_PROFILE value that is neither 0 nor 1, ignored
    };

    /**
     * @brief Reads settings from the text of an ini file.
     *
     * Two sections are read, `[log]` and `[profile]`:
     *
     *     [log]
     *     level = debug          ; trace, debug, info, warn, error or off
     *     keep = 30              ; earlier launches kept in Logs\, 0 for none
     *     level.wxl.probe = info ; a level for one tag: a mod's id, "host", "core"...
     *
     *     [profile]
     *     enabled = 1            ; 0 or 1: the Tracy profiler, off without this line
     *     port = 8086            ; the TCP port a viewer connects to, 1 to 65535
     *     listen = localhost     ; localhost (127.0.0.1 only) or any (every interface)
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

    /**
     * @brief The profiler's settings: the file's, with WXL_PROFILE in the environment (@p env,
     *        null when unset) deciding whether it starts when it is 0 or 1.
     */
    ProfileChoice ResolveProfile(const Settings& settings, const char* env);

    /// The same, with WXL_PROFILE read from the process's environment.
    ProfileChoice ResolveProfile(const Settings& settings);
}
