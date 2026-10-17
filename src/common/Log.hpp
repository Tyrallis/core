// Leveled logging shared by every wxl-core binary (the DLL, the patcher).
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

#include <cstdarg>
#include <cstdint>
#include <string_view>

/**
 * @brief Severity-leveled logging with a file sink, a console sink, and a runtime threshold.
 *
 * Each binary owns one instance (static per module). The file sink writes every line straight
 * to the OS with no user-space buffer: a line that was written is on disk, or at least in the
 * system cache, before the call returns, so a crash of the host process on the next instruction
 * loses nothing. Writes are serialised by a lock and never throw; a sink that cannot be written
 * drops the line and the caller never learns about it, since a log failure must not become a
 * crash of the client.
 *
 * Runtime threshold: the WXL_LOG_LEVEL environment variable (trace|debug|info|warn|error|off,
 * or 0-5), read once at Open; Info when absent.
 */
namespace wxl::log
{
    enum class Level : uint8_t
    {
        Trace = 0, // per-item spam useful only when chasing one specific bug
        Debug = 1, // per-event diagnostics
        Info  = 2, // startup summaries, one-shot confirmations
        Warn  = 3, // unexpected but survivable
        Error = 4, // broken functionality
        Off   = 5, // threshold-only value: disables all output
    };

    /**
     * @brief Opens the file sink. Idempotent; creates the parent directory if missing.
     * @param path  path of the log file, truncated: one file per session.
     * @return true when the file is open after the call.
     */
    bool Open(const wchar_t* path);

    /**
     * @brief Enables the console sink (for CLI tools): Warn and above to stderr, below to stdout.
     * @param prefix  tag prepended to every console line (e.g. "[wxl-core] "), may be null.
     */
    void EnableConsole(const char* prefix);

    /** @brief Sets the runtime threshold, overriding the WXL_LOG_LEVEL default. */
    void SetMinLevel(Level level);

    /** @brief Returns the current runtime threshold. */
    Level MinLevel();

    /** @brief Cheap gate: true when `level` would be written. */
    bool Enabled(Level level);

    /**
     * @brief Writes one already-formatted line. Thread-safe, never throws.
     * @param tag      who is speaking: "core", "patcher", a mod id. Not NUL-terminated.
     * @param message  the line body, without a trailing newline. Not NUL-terminated.
     */
    void WriteLine(Level level, std::string_view tag, std::string_view message);

    /** @brief Formats with printf rules and writes one line under `tag`. Thread-safe. */
    void Write(Level level, const char* tag, const char* fmt, ...);

    /** @brief va_list variant of Write. */
    void WriteV(Level level, const char* tag, const char* fmt, va_list args);

    /** @brief Closes the file sink. Lines written afterwards reach only the console. */
    void Close();
}

#define WXL_LOG_AT(lvl, tag, ...) \
    do { if (::wxl::log::Enabled(lvl)) ::wxl::log::Write(lvl, tag, __VA_ARGS__); } while (0)

#define WLOG_TRACE(tag, ...) WXL_LOG_AT(::wxl::log::Level::Trace, tag, __VA_ARGS__)
#define WLOG_DEBUG(tag, ...) WXL_LOG_AT(::wxl::log::Level::Debug, tag, __VA_ARGS__)
#define WLOG_INFO(tag, ...)  WXL_LOG_AT(::wxl::log::Level::Info,  tag, __VA_ARGS__)
#define WLOG_WARN(tag, ...)  WXL_LOG_AT(::wxl::log::Level::Warn,  tag, __VA_ARGS__)
#define WLOG_ERROR(tag, ...) WXL_LOG_AT(::wxl::log::Level::Error, tag, __VA_ARGS__)
