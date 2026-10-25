// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Leveled logging shared by every wxl-core binary.

#include "common/Log.hpp"

#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>

namespace
{
    using wxl::log::Level;

    // SRWLOCK rather than std::mutex: zero-initialised, no constructor or destructor, so a line
    // written before static construction or during process teardown still takes the lock.
    SRWLOCK            g_lock = SRWLOCK_INIT;
    HANDLE             g_file = INVALID_HANDLE_VALUE;
    bool               g_console = false;
    char               g_consolePrefix[32] = {};
    ULONGLONG          g_lastFlush = 0;

    /// The level of tags without one of their own, and the lowest of every level (the gate).
    std::atomic<Level> g_defaultLevel{ Level::Info };
    std::atomic<Level> g_minLevel{ Level::Info };
    std::atomic<bool>  g_resolved{ false };

    struct TagLevel
    {
        char  tag[64];
        Level level;
    };
    constexpr size_t kMaxTags = 32;
    TagLevel         g_tags[kMaxTags] = {};
    std::atomic<size_t> g_tagCount{ 0 }; // entries are written before the count grows

    /// Fixed-width level name, five characters.
    const char* Column(Level level)
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

    void Recompute()
    {
        Level lowest = g_defaultLevel.load(std::memory_order_relaxed);
        const size_t count = g_tagCount.load(std::memory_order_acquire);
        for (size_t i = 0; i < count; ++i)
            if (g_tags[i].level < lowest) lowest = g_tags[i].level;
        g_minLevel.store(lowest, std::memory_order_relaxed);
    }

    void Resolve()
    {
        if (g_resolved.load(std::memory_order_acquire)) return;
        // Races benignly: both writers store the same value.
        Level fromEnv;
        if (wxl::log::LevelFromEnvironment(fromEnv)) g_defaultLevel.store(fromEnv, std::memory_order_relaxed);
        g_resolved.store(true, std::memory_order_release);
        Recompute();
    }

    /// Copies @p message into @p out, each line break followed by four spaces of indent.
    size_t Indented(char* out, size_t capacity, std::string_view message)
    {
        size_t n = 0;
        for (size_t i = 0; i < message.size() && n + 1 < capacity; ++i)
        {
            const char c = message[i];
            if (c == '\r') continue;
            out[n++] = c;
            if (c == '\n')
                for (int s = 0; s < 4 && n + 1 < capacity; ++s) out[n++] = ' ';
        }
        return n;
    }

    /// Writes a whole line to the file and flushes it as its level asks. The lock is held.
    void ToFile(Level level, const char* line, size_t length, bool forceFlush)
    {
        if (g_file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(g_file, line, static_cast<DWORD>(length), &written, nullptr);
        if (level < Level::Warn && !forceFlush) return;
        const ULONGLONG now = GetTickCount64();
        if (forceFlush || level >= Level::Error || now - g_lastFlush >= 200)
        {
            FlushFileBuffers(g_file);
            g_lastFlush = now;
        }
    }

    /**
     * @brief Writes one finished line to every active sink under the lock.
     *
     * The result of every call is ignored on purpose; a sink that fails drops the line and
     * nothing else happens.
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
        ToFile(level, line, lineLen, false);
        ReleaseSRWLockExclusive(&g_lock);
    }

    /// Formats "[hh:mm:ss.mmm] [LEVEL] [tag] message\n" into @p line; returns its length.
    size_t Format(char* line, size_t capacity, Level level, std::string_view tag, std::string_view message)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        int head = snprintf(line, capacity, "[%02u:%02u:%02u.%03u] [%s] [%.*s] ", t.wHour, t.wMinute,
                            t.wSecond, t.wMilliseconds, Column(level), static_cast<int>(tag.size()),
                            tag.data() ? tag.data() : "");
        if (head < 0) return 0;
        size_t n = static_cast<size_t>(head) < capacity ? static_cast<size_t>(head) : capacity - 1;
        // One fixed buffer: a message longer than it is cut, never allocated for.
        n += Indented(line + n, capacity - n - 1, message.data() ? message : std::string_view());
        line[n++] = '\n';
        return n;
    }

    // --- the session's files ----------------------------------------------------------------

    bool Exists(const wchar_t* path) { return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES; }

    /// Creates every missing folder of @p directory.
    void MakeDirectories(const wchar_t* directory)
    {
        wchar_t partial[512];
        const size_t length = wcsnlen(directory, 511);
        for (size_t i = 0; i <= length; ++i)
        {
            partial[i] = directory[i];
            if ((directory[i] == L'\\' || directory[i] == L'/' || directory[i] == L'\0') && i > 2)
            {
                partial[i] = L'\0';
                CreateDirectoryW(partial, nullptr);
                partial[i] = directory[i];
            }
        }
    }

    /// "YYYY-MM-DD_HH-MM-SS" of the launch that wrote @p path: its first lines, or its last write.
    bool LaunchOf(const wchar_t* path, wchar_t* out, size_t capacity)
    {
        HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        char  head[1024] = {};
        DWORD read       = 0;
        ReadFile(file, head, sizeof head - 1, &read, nullptr);
        FILETIME written{};
        GetFileTime(file, nullptr, nullptr, &written);
        CloseHandle(file);

        if (const char* at = strstr(head, "launch "))
        {
            unsigned y, mo, d, h, mi, s;
            if (sscanf_s(at + 7, "%4u-%2u-%2u %2u:%2u:%2u", &y, &mo, &d, &h, &mi, &s) == 6)
            {
                swprintf(out, capacity, L"%04u-%02u-%02u_%02u-%02u-%02u", y, mo, d, h, mi, s);
                return true;
            }
        }
        FILETIME   local{};
        SYSTEMTIME t{};
        if (!FileTimeToLocalFileTime(&written, &local) || !FileTimeToSystemTime(&local, &t)) return false;
        swprintf(out, capacity, L"%04u-%02u-%02u_%02u-%02u-%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond);
        return true;
    }

    /// Deletes the oldest `<stem>-*.log` of @p directory until @p keep remain; counts both.
    void Prune(const wchar_t* directory, const wchar_t* stem, unsigned keep, unsigned& kept, unsigned& removed)
    {
        // The names carry their launch time, so their order is their age. Kept on the stack:
        // a folder holding more than this is pruned over the next launches.
        constexpr size_t kMax = 256;
        wchar_t          names[kMax][96];
        size_t           count = 0;

        wchar_t pattern[640];
        swprintf(pattern, 640, L"%ls\\%ls-*.log", directory, stem);
        WIN32_FIND_DATAW found;
        HANDLE           search = FindFirstFileW(pattern, &found);
        if (search != INVALID_HANDLE_VALUE)
        {
            do
            {
                if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                if (wcslen(found.cFileName) >= 96 || count == kMax) continue;
                wcscpy_s(names[count++], found.cFileName);
            } while (FindNextFileW(search, &found));
            FindClose(search);
        }
        // Insertion sort, oldest first.
        for (size_t i = 1; i < count; ++i)
            for (size_t j = i; j > 0 && wcscmp(names[j - 1], names[j]) > 0; --j)
            {
                wchar_t swap[96];
                wcscpy_s(swap, names[j]);
                wcscpy_s(names[j], names[j - 1]);
                wcscpy_s(names[j - 1], swap);
            }
        size_t i = 0;
        for (; count - i > keep; ++i)
        {
            wchar_t path[640];
            swprintf(path, 640, L"%ls\\%ls", directory, names[i]);
            if (DeleteFileW(path)) ++removed;
        }
        kept = static_cast<unsigned>(count - i);
    }
}

namespace wxl::log
{
    bool ParseLevel(std::string_view text, Level& out)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t')) text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
            text.remove_suffix(1);
        if (text.size() == 1 && text[0] >= '0' && text[0] <= '5')
        {
            out = static_cast<Level>(text[0] - '0');
            return true;
        }
        static const struct { const char* name; Level level; } kNames[] = {
            {"trace", Level::Trace}, {"debug", Level::Debug}, {"info", Level::Info},
            {"warn", Level::Warn},   {"warning", Level::Warn}, {"error", Level::Error},
            {"off", Level::Off},
        };
        for (const auto& known : kNames)
        {
            const size_t length = strlen(known.name);
            if (text.size() != length) continue;
            bool same = true;
            for (size_t i = 0; i < length && same; ++i) same = (text[i] | 0x20) == known.name[i];
            if (same)
            {
                out = known.level;
                return true;
            }
        }
        return false;
    }

    const char* LevelName(Level level)
    {
        switch (level)
        {
        case Level::Trace: return "trace";
        case Level::Debug: return "debug";
        case Level::Info:  return "info";
        case Level::Warn:  return "warn";
        case Level::Error: return "error";
        case Level::Off:   return "off";
        }
        return "?";
    }

    bool LevelFromEnvironment(Level& out)
    {
        char raw[16] = {};
        const DWORD n = GetEnvironmentVariableA("WXL_LOG_LEVEL", raw, sizeof raw);
        if (n == 0 || n >= sizeof raw) return false;
        return ParseLevel(std::string_view(raw, n), out);
    }

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
        Resolve(); // the threshold now, on the opener's thread
        return open;
    }

    bool OpenSession(const wchar_t* directory, const wchar_t* stem, unsigned keep, SessionFiles* files)
    {
        SessionFiles local;
        SessionFiles& out = files ? *files : local;
        if (!directory || !stem) return false;

        AcquireSRWLockExclusive(&g_lock);
        if (g_file == INVALID_HANDLE_VALUE)
        {
            MakeDirectories(directory);
            swprintf(out.path, 512, L"%ls\\%ls.log", directory, stem);

            if (Exists(out.path))
            {
                wchar_t launch[32];
                if (keep > 0 && LaunchOf(out.path, launch, 32))
                {
                    for (unsigned attempt = 1; attempt < 100 && !out.kept_previous; ++attempt)
                    {
                        if (attempt == 1) swprintf(out.previous, 96, L"%ls-%ls.log", stem, launch);
                        else swprintf(out.previous, 96, L"%ls-%ls-%u.log", stem, launch, attempt);
                        wchar_t target[640];
                        swprintf(target, 640, L"%ls\\%ls", directory, out.previous);
                        if (Exists(target)) continue;
                        out.kept_previous = MoveFileExW(out.path, target, 0) != 0;
                        if (!out.kept_previous) break;
                    }
                }
                Prune(directory, stem, keep, out.kept, out.removed);
            }
            else
            {
                Prune(directory, stem, keep, out.kept, out.removed);
            }

            // FILE_SHARE_READ: the file can be opened in an editor while the client runs.
            g_file = CreateFileW(out.path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
            if (g_file == INVALID_HANDLE_VALUE)
            {
                swprintf(out.path, 512, L"%ls\\%ls-pid%lu.log", directory, stem, GetCurrentProcessId());
                g_file = CreateFileW(out.path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
                out.fallback = g_file != INVALID_HANDLE_VALUE;
            }
        }
        const bool open = g_file != INVALID_HANDLE_VALUE;
        ReleaseSRWLockExclusive(&g_lock);
        Resolve();
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
        g_resolved.store(true, std::memory_order_release);
        g_defaultLevel.store(level, std::memory_order_relaxed);
        Recompute();
    }

    bool SetTagLevel(std::string_view tag, Level level)
    {
        if (tag.empty() || tag.size() >= sizeof g_tags[0].tag) return false;
        Resolve();
        AcquireSRWLockExclusive(&g_lock);
        const size_t count = g_tagCount.load(std::memory_order_relaxed);
        bool         set   = false;
        for (size_t i = 0; i < count && !set; ++i)
        {
            if (tag == g_tags[i].tag)
            {
                g_tags[i].level = level;
                set             = true;
            }
        }
        if (!set && count < kMaxTags)
        {
            memcpy(g_tags[count].tag, tag.data(), tag.size());
            g_tags[count].tag[tag.size()] = '\0';
            g_tags[count].level           = level;
            g_tagCount.store(count + 1, std::memory_order_release);
            set = true;
        }
        ReleaseSRWLockExclusive(&g_lock);
        Recompute();
        return set;
    }

    Level MinLevel()
    {
        Resolve();
        return g_minLevel.load(std::memory_order_relaxed);
    }

    Level LevelFor(std::string_view tag)
    {
        Resolve();
        const size_t count = g_tagCount.load(std::memory_order_acquire);
        for (size_t i = 0; i < count; ++i)
            if (tag == g_tags[i].tag) return g_tags[i].level;
        return g_defaultLevel.load(std::memory_order_relaxed);
    }

    bool Enabled(Level level)
    {
        return level != Level::Off && level >= MinLevel();
    }

    void WriteLine(Level level, std::string_view tag, std::string_view message)
    {
        if (!Enabled(level)) return;
        if (g_tagCount.load(std::memory_order_relaxed) != 0 && level < LevelFor(tag)) return;

        char         line[2048];
        const size_t n = Format(line, sizeof line, level, tag, message);
        if (n == 0) return;
        Emit(level, line, n, message);
    }

    void WriteUnfiltered(Level level, std::string_view tag, std::string_view message)
    {
        char         line[2048];
        const size_t n = Format(line, sizeof line, level, tag, message);
        if (n == 0) return;
        Emit(level, line, n, message);
    }

    void WriteUnfilteredF(Level level, const char* tag, const char* fmt, ...)
    {
        char    body[1536];
        va_list args;
        va_start(args, fmt);
        const int n = vsnprintf(body, sizeof body, fmt, args);
        va_end(args);
        if (n < 0) return;
        const size_t len = static_cast<size_t>(n) < sizeof body ? static_cast<size_t>(n) : sizeof body - 1;
        WriteUnfiltered(level, tag ? std::string_view(tag) : std::string_view("?"), std::string_view(body, len));
    }

    void WriteUrgent(std::string_view tag, std::string_view message, bool flush)
    {
        char         line[2048];
        const size_t n = Format(line, sizeof line, Level::Error, tag, message);
        if (n == 0) return;

        // About 100 ms for the lock; past that, the holder is taken to be the crashed thread
        // itself, and the line is written without it.
        bool locked = false;
        for (int i = 0; i < 100 && !(locked = TryAcquireSRWLockExclusive(&g_lock)); ++i) Sleep(1);
        if (g_file != INVALID_HANDLE_VALUE)
        {
            DWORD written = 0;
            WriteFile(g_file, line, static_cast<DWORD>(n), &written, nullptr);
            if (flush) FlushFileBuffers(g_file);
        }
        if (locked) ReleaseSRWLockExclusive(&g_lock);
    }

    void WriteV(Level level, const char* tag, const char* fmt, va_list args)
    {
        if (!Enabled(level)) return;
        char body[1536];
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

    void Flush()
    {
        AcquireSRWLockExclusive(&g_lock);
        if (g_file != INVALID_HANDLE_VALUE) FlushFileBuffers(g_file);
        g_lastFlush = GetTickCount64();
        ReleaseSRWLockExclusive(&g_lock);
    }

    void Close()
    {
        AcquireSRWLockExclusive(&g_lock);
        if (g_file != INVALID_HANDLE_VALUE)
        {
            FlushFileBuffers(g_file);
            CloseHandle(g_file);
            g_file = INVALID_HANDLE_VALUE;
        }
        ReleaseSRWLockExclusive(&g_lock);
    }
}
