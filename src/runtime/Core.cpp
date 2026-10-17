// The core runtime: paths, the log sink, and the mod host.
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

#include "runtime/Core.hpp"

#include "common/Log.hpp"

#include "wxl/host/mod_host.hpp"

#include <windows.h>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    constexpr wchar_t kLogRelative[]  = L"Logs\\WarcraftXL.log";
    constexpr wchar_t kModsRelative[] = L"Mods";

    /**
     * @brief Writes the folder of the process executable into buf, with a trailing backslash.
     * @param buf  receives the NUL-terminated path.
     * @param cap  capacity of buf in characters.
     * @return Length of the written path, or 0 when it does not fit or cannot be read.
     */
    size_t ExeDirectory(wchar_t* buf, size_t cap)
    {
        const DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(cap));
        if (n == 0 || n >= cap) return 0;
        for (size_t i = n; i-- > 0;)
        {
            if (buf[i] == L'\\' || buf[i] == L'/')
            {
                buf[i + 1] = L'\0';
                return i + 1;
            }
        }
        return 0;
    }

    wxl::log::Level ToCoreLevel(wxl_log_level level)
    {
        switch (level)
        {
        case WXL_LOG_TRACE: return wxl::log::Level::Trace;
        case WXL_LOG_DEBUG: return wxl::log::Level::Debug;
        case WXL_LOG_INFO:  return wxl::log::Level::Info;
        case WXL_LOG_WARN:  return wxl::log::Level::Warn;
        case WXL_LOG_ERROR: return wxl::log::Level::Error;
        }
        return wxl::log::Level::Info;
    }

    wxl_log_level ToHostLevel(wxl::log::Level level)
    {
        switch (level)
        {
        case wxl::log::Level::Trace: return WXL_LOG_TRACE;
        case wxl::log::Level::Debug: return WXL_LOG_DEBUG;
        case wxl::log::Level::Info:  return WXL_LOG_INFO;
        case wxl::log::Level::Warn:  return WXL_LOG_WARN;
        default:                     return WXL_LOG_ERROR;
        }
    }

    /// Routes the host's and every mod's lines into the core log. Never throws.
    class CoreSink final : public wxl::host::LogSink
    {
    public:
        void Write(wxl_log_level level, std::string_view tag, std::string_view message) override
        {
            wxl::log::WriteLine(ToCoreLevel(level), tag, message);
        }
    };

    CoreSink g_sink;

    // Heap-allocated and never freed: the wxl_host table mods keep points into it, and no seam
    // exists yet from which the mods could be told to let go before the process ends.
    wxl::host::ModHost* g_host = nullptr;
}

namespace wxl::core
{
    bool OpenLog()
    {
        wchar_t path[1024];
        const size_t dir = ExeDirectory(path, sizeof path / sizeof path[0]);
        if (dir == 0) return false;
        if (wcscat_s(path, kLogRelative) != 0) return false;
        return wxl::log::Open(path);
    }

    void Start()
    {
        try
        {
            wchar_t dir[1024];
            if (ExeDirectory(dir, sizeof dir / sizeof dir[0]) == 0)
            {
                WLOG_ERROR(kTag, "cannot resolve the client folder; no mods loaded");
                return;
            }

            wxl::host::HostConfig cfg;
            cfg.mods_directory  = std::filesystem::path(dir) / kModsRelative;
            cfg.runtime_id      = kRuntimeId;
            cfg.runtime_name    = kRuntimeName;
            cfg.runtime_version = WXL_CORE_VERSION;
            cfg.log_sink        = &g_sink;
            cfg.log_minimum     = ToHostLevel(wxl::log::MinLevel());
            // No events are raised and no services are published in this slice: a mod that
            // requires a service is refused by the host with the service's name.

            WLOG_INFO(kTag, "runtime %s (%s) version %u, abi %u.%u, target %s, mods in %ls",
                      kRuntimeId, kRuntimeName, static_cast<unsigned>(WXL_CORE_VERSION),
                      static_cast<unsigned>(WXL_ABI_VERSION_MAJOR),
                      static_cast<unsigned>(WXL_ABI_VERSION_MINOR), cfg.target.c_str(),
                      cfg.mods_directory.c_str());

            g_host = new wxl::host::ModHost(std::move(cfg));
            g_host->LoadAll();
            g_host->Stage(WXL_STAGE_EARLY);

            for (const wxl::host::LoadedMod& mod : g_host->Loaded())
                WLOG_INFO(kTag, "mod %s %s (%s) is up", mod.id.c_str(),
                          mod.manifest.version.text.c_str(), mod.name.c_str());
            for (const wxl::host::RefusedMod& mod : g_host->Refused())
                WLOG_INFO(kTag, "mod %s was not loaded: %s",
                          mod.id.empty() ? mod.directory_name.c_str() : mod.id.c_str(),
                          mod.reason.c_str());
            WLOG_INFO(kTag, "core ready: %zu mod(s) loaded", g_host->Loaded().size());
        }
        catch (const std::exception& e)
        {
            WLOG_ERROR(kTag, "mod host failed: %s", e.what());
        }
        catch (...)
        {
            WLOG_ERROR(kTag, "mod host failed: unknown exception");
        }
    }
}
