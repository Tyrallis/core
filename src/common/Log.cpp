// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Leveled logging shared by every wxl-core binary.

#include "common/Log.hpp"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace
{
    using wxl::log::Level;

    // SRWLOCK rather than std::mutex: zero-initialised, no constructor or destructor, so a line
    // written before static construction or during process teardown still takes the lock.
    SRWLOCK            g_lock = SRWLOCK_INIT;
    HANDLE             g_file = INVALID_HANDLE_VALUE;
    std::atomic<Level> g_minLevel{ Level::Info };
    bool               g_minLevelResolved = false;
    bool               g_console = false;
    char               g_consolePrefix[32] = {};

    /** @brief Parses WXL_LOG_LEVEL ("debug", "warn", "2", ...) once; Info when absent/garbled. */
    Level LevelFromEnv()
    {
        char raw[16] = {};
        const DWORD n = GetEnvironmentVariableA("WXL_LOG_LEVEL", raw, sizeof raw);
        if (n == 0 || n >= sizeof raw) return Level::Info;
        if (*raw >= '0' && *raw <= '5') return static_cast<Level>(*raw - '0');
        switch (*raw | 0x20)
        {
        case 't': return Level::Trace;
        case 'd': return Level::Debug;
        case 'i': return Level::Info;
        case 'w': return Level::Warn;
        case 'e': return Level::Error;
        case 'o': return Level::Off;
        default:  return Level::Info;
        }
    }

    /** @brief Fixed-width level name, five characters. */
    const char* Name(Level level)
    {
        switch (level)
        {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
        default:           return "?????";
        }
    }

    /**
     * @brief Writes one finished line to every active sink under the lock.
     *
     * The file write goes to the OS at once: there is no user-space buffer to lose. The result of
     * every call is ignored on purpose; a sink that fails drops the line and nothing else happens.
     * @param level    severity, routes console output to stdout or stderr.
     * @param line     fully formatted file line, newline-terminated.
     * @param lineLen  byte length of line.
     * @param message  the bare message, for the console sink.
     */
    void Emit(Level level, const char* line, size_t lineLen, std::string_view message)
    {
        AcquireSRWLockExclusive(&g_lock);
        if (IsDebuggerPresent()) OutputDebugStringA(line);
        if (g_console)
        {
            FILE* out = level >= Level::Warn ? stderr : stdout;
            if (g_consolePrefix[0]) fputs(g_consolePrefix, out);
            if (level == Level::Warn) fputs("warning: ", out);
            else if (level == Level::Error) fputs("error: ", out);
            fwrite(message.data(), 1, message.size(), out);
            fputc('\n', out);
        }
        if (g_file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(g_file, line, static_cast<DWORD>(lineLen), &written, nullptr);
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
}

namespace wxl::log
{
    bool Open(const wchar_t* path)
    {
        if (!path) return false;
        AcquireSRWLockExclusive(&g_lock);
        if (g_file == INVALID_HANDLE_VALUE)
        {
            // Create the parent directory so a missing Logs\ folder cannot silently eat every line.
            wchar_t dir[MAX_PATH];
            const size_t len = wcsnlen(path, MAX_PATH);
            if (len < MAX_PATH)
            {
                memcpy(dir, path, (len + 1) * sizeof(wchar_t));
                for (size_t i = len; i-- > 0;)
                {
                    if (dir[i] == L'\\' || dir[i] == L'/')
                    {
                        dir[i] = L'\0';
                        if (dir[0]) CreateDirectoryW(dir, nullptr);
                        break;
                    }
                }
            }

            // FILE_SHARE_READ: the file can be opened in an editor while the client runs.
            g_file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        }
        const bool open = g_file != INVALID_HANDLE_VALUE;
        ReleaseSRWLockExclusive(&g_lock);
        MinLevel(); // resolve the threshold now, on the opener's thread
        return open;
    }

    void EnableConsole(const char* prefix)
    {
        AcquireSRWLockExclusive(&g_lock);
        g_console = true;
        if (prefix) strncpy_s(g_consolePrefix, prefix, _TRUNCATE);
        ReleaseSRWLockExclusive(&g_lock);
    }

    void SetMinLevel(Level level)
    {
        g_minLevelResolved = true;
        g_minLevel.store(level, std::memory_order_relaxed);
    }

    Level MinLevel()
    {
        if (!g_minLevelResolved)
        {
            // Resolved lazily on first use; races benignly (both writers store the same value).
            g_minLevel.store(LevelFromEnv(), std::memory_order_relaxed);
            g_minLevelResolved = true;
        }
        return g_minLevel.load(std::memory_order_relaxed);
    }

    bool Enabled(Level level)
    {
        return level >= MinLevel();
    }

    void WriteLine(Level level, std::string_view tag, std::string_view message)
    {
        if (!Enabled(level)) return;

        SYSTEMTIME t;
        GetLocalTime(&t);

        // One fixed buffer: a message longer than it is cut, never allocated for. The newline is
        // restored after a cut so the next line still starts on its own row.
        char line[2048];
        int n = snprintf(line, sizeof line, "[%02u:%02u:%02u.%03u] [%s] [%.*s] %.*s\n",
                         t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, Name(level),
                         static_cast<int>(tag.size()), tag.data() ? tag.data() : "",
                         static_cast<int>(message.size()), message.data() ? message.data() : "");
        if (n < 0) return;
        if (static_cast<size_t>(n) >= sizeof line)
        {
            n = sizeof line - 1;
            line[n - 1] = '\n';
        }
        Emit(level, line, static_cast<size_t>(n), message);
    }

    void WriteV(Level level, const char* tag, const char* fmt, va_list args)
    {
        if (!Enabled(level)) return;
        char body[1024];
        const int n = vsnprintf(body, sizeof body, fmt, args);
        if (n < 0) return;
        const size_t len = static_cast<size_t>(n) < sizeof body ? static_cast<size_t>(n) : sizeof body - 1;
        WriteLine(level, tag ? std::string_view(tag) : std::string_view("?"), std::string_view(body, len));
    }

    void Write(Level level, const char* tag, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        WriteV(level, tag, fmt, args);
        va_end(args);
    }

    void Close()
    {
        AcquireSRWLockExclusive(&g_lock);
        if (g_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(g_file);
            g_file = INVALID_HANDLE_VALUE;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
}
