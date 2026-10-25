// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Leveled logging shared by every wxl-core binary (the DLL, the patcher).

#pragma once

#include <cstdarg>
#include <cstdint>
#include <string_view>

/**
 * @brief Severity-leveled logging with a file sink, a console sink, and a runtime threshold.
 *
 * Each binary owns one instance (static per module). The file sink writes every line straight
 * to the OS with no user-space buffer: a line that was written is in the system's hands before
 * the call returns, so a crash of the host process on the next instruction loses nothing. A
 * warning or an error is also flushed to the disk (an error always, warnings at most every
 * 200 ms), for a machine that goes down with the process. Writes are serialised by a lock and
 * never throw; a sink that cannot be written drops the line and the caller never learns about
 * it, since a log failure must not become a crash of the client.
 *
 * A message holding line breaks keeps them, each following line indented by four spaces, so a
 * reader tells a continuation (a Lua traceback) from the next line.
 *
 * Threshold: one level for every tag, and optionally a level of its own for some tags (a mod's
 * id, "host"). The WXL_LOG_LEVEL environment variable (trace|debug|info|warn|error|off, or 0-5)
 * is read on first use; Info when absent. The runtime may set the levels from its settings file.
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

    /// Parses a level name ("debug", "WARN", "2"); false when it names none.
    bool ParseLevel(std::string_view text, Level& out);

    /// "trace", "debug", "info", "warn", "error", "off".
    const char* LevelName(Level level);

    /**
     * @brief Opens the file sink at @p path, truncated. Idempotent; creates the parent
     *        directory if missing. For a tool's own log; the runtime uses @ref OpenSession.
     * @return true when the file is open after the call.
     */
    bool Open(const wchar_t* path);

    /// What @ref OpenSession did with the previous launch's log.
    struct SessionFiles
    {
        bool     kept_previous = false; ///< the previous log was renamed, not overwritten
        wchar_t  previous[96]  = {};    ///< its new file name, in the same folder
        unsigned kept          = 0;     ///< earlier logs in the folder after pruning
        unsigned removed       = 0;     ///< the oldest ones deleted to stay within keep
        bool     fallback      = false; ///< the log is open under another name (see path)
        wchar_t  path[512]     = {};    ///< the file the lines go to
    };

    /**
     * @brief Opens `<directory>\<stem>.log` for this launch, keeping the previous launch's.
     *
     * A `<stem>.log` already there is the previous launch: it is renamed
     * `<stem>-<its launch time>.log` (the time read from its first lines, which the runtime
     * writes as "launch YYYY-MM-DD HH:MM:SS", or its last write when it has none), then the
     * oldest `<stem>-*.log` are deleted until @p keep remain. With @p keep 0 the previous log is
     * deleted. When the file cannot be taken (another client running from the same folder
     * holds it), the lines go to `<stem>-pid<pid>.log` instead.
     *
     * Kernel32 only, safe under the loader lock. @p files may be null.
     */
    bool OpenSession(const wchar_t* directory, const wchar_t* stem, unsigned keep, SessionFiles* files);

    /**
     * @brief Enables the console sink (for CLI tools): Warn and above to stderr, below to stdout.
     * @param prefix  tag prepended to every console line (e.g. "[wxl-core] "), may be null.
     */
    void EnableConsole(const char* prefix);

    /// Sets the level of every tag without one of its own, overriding WXL_LOG_LEVEL.
    void SetMinLevel(Level level);

    /**
     * @brief Gives @p tag a level of its own, above or below the others. At most 32 tags; a
     *        tag given twice keeps the last level.
     * @return false when the table is full or the tag is empty or longer than 63 bytes.
     */
    bool SetTagLevel(std::string_view tag, Level level);

    /// The level WXL_LOG_LEVEL asks for; false when it is absent or names none.
    bool LevelFromEnvironment(Level& out);

    /**
     * @brief The lowest level any tag is written at: what a line must reach to have a chance.
     *        A source that filters before it calls (the mod host) filters at this one.
     */
    Level MinLevel();

    /// The level lines of @p tag are written at.
    Level LevelFor(std::string_view tag);

    /// Cheap gate: true when `level` would be written for some tag.
    bool Enabled(Level level);

    /**
     * @brief Writes one already-formatted line, when its tag's level lets it. Thread-safe,
     *        never throws.
     * @param tag      who is speaking: "core", "patcher", a mod id. Not NUL-terminated.
     * @param message  the line body, without a trailing newline. Not NUL-terminated.
     */
    void WriteLine(Level level, std::string_view tag, std::string_view message);

    /**
     * @brief Writes a line whatever the levels: the launch's header and its end, which a
     *        reader of the log relies on finding.
     */
    void WriteUnfiltered(Level level, std::string_view tag, std::string_view message);

    /// Formats with printf rules and writes one line with @ref WriteUnfiltered.
    void WriteUnfilteredF(Level level, const char* tag, const char* fmt, ...);

    /**
     * @brief Writes an error line from a crash handler: no level filter, and the lock is waited
     *        for a moment only, since the crashing thread may be the one holding it. Allocates
     *        nothing. Flushed to the disk when @p flush is set.
     */
    void WriteUrgent(std::string_view tag, std::string_view message, bool flush);

    /// Formats with printf rules and writes one line under `tag`. Thread-safe.
    void Write(Level level, const char* tag, const char* fmt, ...);

    /// va_list variant of Write.
    void WriteV(Level level, const char* tag, const char* fmt, va_list args);

    /// Flushes the file to the disk.
    void Flush();

    /// Closes the file sink. Lines written afterwards reach only the console.
    void Close();
}

#define WXL_LOG_AT(lvl, tag, ...) \
    do { if (::wxl::log::Enabled(lvl)) ::wxl::log::Write(lvl, tag, __VA_ARGS__); } while (0)

#define WLOG_TRACE(tag, ...) WXL_LOG_AT(::wxl::log::Level::Trace, tag, __VA_ARGS__)
#define WLOG_DEBUG(tag, ...) WXL_LOG_AT(::wxl::log::Level::Debug, tag, __VA_ARGS__)
#define WLOG_INFO(tag, ...)  WXL_LOG_AT(::wxl::log::Level::Info,  tag, __VA_ARGS__)
#define WLOG_WARN(tag, ...)  WXL_LOG_AT(::wxl::log::Level::Warn,  tag, __VA_ARGS__)
#define WLOG_ERROR(tag, ...) WXL_LOG_AT(::wxl::log::Level::Error, tag, __VA_ARGS__)
