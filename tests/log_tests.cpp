// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The log: a launch's file kept for the next, the oldest pruned, levels by tag, continuation
// lines, and WarcraftXL.ini. Run with a scratch folder:
//
//     wxl-core-log-tests <scratch>

#include "common/Log.hpp"
#include "runtime/Settings.hpp"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* what)
    {
        std::printf("  %s  %s\n", condition ? "ok  " : "FAIL", what);
        if (!condition) ++g_failures;
    }

    std::wstring Wide(const char* text)
    {
        std::wstring out;
        for (; *text; ++text) out += static_cast<wchar_t>(*text == '/' ? '\\' : *text);
        return out;
    }

    std::string Read(const std::wstring& path)
    {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return std::string();
        std::string text(1 << 16, '\0');
        DWORD       read = 0;
        ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr);
        CloseHandle(file);
        text.resize(read);
        return text;
    }

    std::vector<std::wstring> Logs(const std::wstring& dir)
    {
        std::vector<std::wstring> names;
        WIN32_FIND_DATAW          found;
        HANDLE                    search = FindFirstFileW((dir + L"\\*.log").c_str(), &found);
        if (search == INVALID_HANDLE_VALUE) return names;
        do names.push_back(found.cFileName);
        while (FindNextFileW(search, &found));
        FindClose(search);
        return names;
    }

    bool Has(const std::vector<std::wstring>& names, const wchar_t* name)
    {
        for (const std::wstring& one : names)
            if (one == name) return true;
        return false;
    }

    /// One launch: opens the session, writes its first line, closes.
    wxl::log::SessionFiles Launch(const std::wstring& dir, unsigned keep, const char* launch)
    {
        wxl::log::SessionFiles files;
        wxl::log::OpenSession(dir.c_str(), L"WarcraftXL", keep, &files);
        if (launch) wxl::log::WriteUnfiltered(wxl::log::Level::Info, "session", launch);
        wxl::log::Close();
        return files;
    }
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2)
    {
        std::printf("usage: wxl-core-log-tests <scratch>\n");
        return 2;
    }
    const std::wstring scratch = Wide(argv[1]);
    CreateDirectoryW(scratch.c_str(), nullptr);

    std::printf("a launch's log is kept for the next\n");
    {
        const std::wstring dir = scratch + L"\\Logs";
        for (const std::wstring& name : Logs(dir)) DeleteFileW((dir + L"\\" + name).c_str());

        wxl::log::SessionFiles first = Launch(dir, 2, "launch 2026-10-25 14:03:22 local, pid 1");
        Check(!first.kept_previous && Logs(dir).size() == 1 && Has(Logs(dir), L"WarcraftXL.log"),
              "the first launch writes WarcraftXL.log, creating Logs\\");
        Check(Read(dir + L"\\WarcraftXL.log").find("] [INFO ] [session] launch 2026-10-25 14:03:22") != std::string::npos,
              "with the line the next launch reads its time from");

        wxl::log::SessionFiles second = Launch(dir, 2, "launch 2026-10-25 15:00:00 local, pid 2");
        Check(second.kept_previous && std::wstring(second.previous) == L"WarcraftXL-2026-10-25_14-03-22.log" &&
                  Has(Logs(dir), L"WarcraftXL-2026-10-25_14-03-22.log") && second.kept == 1,
              "the second renames it after its launch time");
        Check(Read(dir + L"\\WarcraftXL.log").find("15:00:00") != std::string::npos &&
                  Read(dir + L"\\WarcraftXL-2026-10-25_14-03-22.log").find("14:03:22") != std::string::npos,
              "each file holds its own launch");

        Launch(dir, 2, "launch 2026-10-26 09:00:00 local, pid 3");
        wxl::log::SessionFiles fourth = Launch(dir, 2, "launch 2026-10-26 10:00:00 local, pid 4");
        const auto names = Logs(dir);
        Check(names.size() == 3 && !Has(names, L"WarcraftXL-2026-10-25_14-03-22.log") &&
                  Has(names, L"WarcraftXL-2026-10-25_15-00-00.log") && Has(names, L"WarcraftXL-2026-10-26_09-00-00.log") &&
                  fourth.kept == 2 && fourth.removed == 1,
              "past keep, the oldest go");

        // A launch that wrote the same time (two launches in one second).
        Launch(dir, 2, "launch 2026-10-26 10:00:00 local, pid 5");
        Check(Has(Logs(dir), L"WarcraftXL-2026-10-26_10-00-00.log"), "a name taken already is not overwritten");
        Launch(dir, 5, "launch 2026-10-26 10:00:00 local, pid 6");
        Check(Has(Logs(dir), L"WarcraftXL-2026-10-26_10-00-00-2.log"), "the next one is numbered");

        Launch(dir, 5, nullptr);
        Launch(dir, 5, "launch 2026-10-27 07:00:00 local, pid 8");
        bool dated = false;
        for (const std::wstring& name : Logs(dir))
            dated = dated || (name.size() == std::wstring(L"WarcraftXL-0000-00-00_00-00-00.log").size() &&
                              name.compare(0, 11, L"WarcraftXL-") == 0);
        Check(dated, "a log without a launch line is named after its last write");

        Launch(dir, 0, "launch 2026-10-27 08:00:00 local, pid 7");
        Check(Logs(dir).size() == 1, "keep 0 keeps none");
    }

    std::printf("levels\n");
    {
        using wxl::log::Level;
        const std::wstring dir = scratch + L"\\Levels";
        wxl::log::SessionFiles files;
        wxl::log::OpenSession(dir.c_str(), L"WarcraftXL", 0, &files);
        wxl::log::SetMinLevel(Level::Warn);
        Check(wxl::log::MinLevel() == Level::Warn && !wxl::log::Enabled(Level::Info), "one level for every tag");
        Check(wxl::log::SetTagLevel("chatty", Level::Debug) && wxl::log::SetTagLevel("quiet", Level::Error),
              "a tag gets a level of its own");
        Check(wxl::log::MinLevel() == Level::Debug && wxl::log::Enabled(Level::Debug) &&
                  wxl::log::LevelFor("chatty") == Level::Debug && wxl::log::LevelFor("other") == Level::Warn,
              "the gate opens to the lowest of them");
        wxl::log::WriteLine(Level::Info, "core", "not written: core is at warn");
        wxl::log::WriteLine(Level::Debug, "chatty", "written: chatty is at debug");
        wxl::log::WriteLine(Level::Warn, "quiet", "not written: quiet is at error");
        wxl::log::WriteLine(Level::Error, "core", "first line\nstack traceback:\n\tinit.lua:3");
        wxl::log::WriteUnfiltered(Level::Info, "session", "written whatever the levels");
        wxl::log::WriteUrgent("crash", "urgent", true);
        wxl::log::Close();
        const std::string text = Read(files.path);
        Check(text.find("not written") == std::string::npos && text.find("[DEBUG] [chatty] written") != std::string::npos,
              "each tag is filtered at its own level");
        Check(text.find("[ERROR] [core] first line\n    stack traceback:\n    \tinit.lua:3\n") != std::string::npos,
              "a message's further lines are indented under it");
        Check(text.find("[session] written whatever") != std::string::npos &&
                  text.find("[ERROR] [crash] urgent") != std::string::npos,
              "an unfiltered line and an urgent one are written");
        wxl::log::SetMinLevel(Level::Info);
    }

    std::printf("WarcraftXL.ini\n");
    {
        using wxl::log::Level;
        const char text[] = "\xEF\xBB\xBF; WarcraftXL settings\r\n"
                            "[log]\r\n"
                            "level = Debug   ; everything but trace\r\n"
                            "keep=5\r\n"
                            "level.wxl.probe = trace\r\n"
                            "level.host = warn\r\n"
                            "colour = blue\r\n"
                            "level.core = loud\r\n"
                            "[other]\r\n"
                            "level = error\r\n";
        wxl::core::settings::Settings settings;
        wxl::core::settings::Parse(text, sizeof text - 1, settings);
        Check(settings.found && settings.has_level && settings.level == Level::Debug && settings.keep == 5,
              "[log] level and keep, with comments, CRLF and a byte order mark");
        Check(settings.tag_count == 2 && std::strcmp(settings.tags[0].name, "wxl.probe") == 0 &&
                  settings.tags[0].level == Level::Trace && settings.tags[1].level == Level::Warn,
              "level.<tag>, a tag holding dots included");
        Check(settings.problem_count == 2 && std::strstr(settings.problems[0], "line 7") &&
                  std::strstr(settings.problems[0], "colour") && std::strstr(settings.problems[1], "loud"),
              "an unknown key and a level that does not read are noted with their line");

        wxl::core::settings::Settings none;
        wxl::core::settings::Load((scratch + L"\\no-such.ini").c_str(), none);
        Check(!none.found && !none.has_level && none.keep == 30, "without the file, the defaults");

        Level level;
        Check(wxl::log::ParseLevel(" WARN ", level) && level == Level::Warn && wxl::log::ParseLevel("1", level) &&
                  level == Level::Debug && !wxl::log::ParseLevel("loud", level),
              "level names, in any case, or 0 to 5");
    }

    std::printf(g_failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", g_failures);
    return g_failures ? 1 : 0;
}
