// The core runtime: paths, the log sink, the services, and the mod host.
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
#include "hook/Engine.hpp"
#include "runtime/Image.hpp"
#include "runtime/WorldSeam.hpp"
#include "services/HookService.hpp"
#include "services/PatchService.hpp"

#include "wxl/host/mod_host.hpp"
#include "wxl/services/hook.h"
#include "wxl/services/patch.h"

#include <windows.h>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    constexpr wchar_t kLogRelative[]  = L"Logs\\WarcraftXL.log";
    constexpr wchar_t kModsRelative[] = L"Mods";
    constexpr char    kEntrySection[] = ".wxle";

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

    LONG g_started = 0;
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

    bool HasEntryTrampoline()
    {
        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

        const DWORD entry = nt->OptionalHeader.AddressOfEntryPoint;
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (memcmp(sec[i].Name, kEntrySection, sizeof kEntrySection) != 0) continue;
            return entry >= sec[i].VirtualAddress
                && entry < sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
        }
        return false;
    }

    wxl::host::ModHost* Host() { return g_host; }

    void Start()
    {
        if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
        {
            WLOG_WARN(kTag, "Start called twice; the second call is ignored");
            return;
        }

        try
        {
            wchar_t dir[1024];
            if (ExeDirectory(dir, sizeof dir / sizeof dir[0]) == 0)
            {
                WLOG_ERROR(kTag, "cannot resolve the client folder; no mods loaded");
                return;
            }

            if (!wxl::hook::Init())
                WLOG_ERROR(kTag, "hook engine unavailable; detours will be refused");

            wxl::host::HostConfig cfg;
            cfg.mods_directory  = std::filesystem::path(dir) / kModsRelative;
            cfg.runtime_id      = kRuntimeId;
            cfg.runtime_name    = kRuntimeName;
            cfg.runtime_version = WXL_CORE_VERSION;
            cfg.log_sink        = &g_sink;
            cfg.log_minimum     = ToHostLevel(wxl::log::MinLevel());
            cfg.events          = { WXL_EV_WorldEnter };
            cfg.services        = {
                { WXL_HOOK_SERVICE, WXL_HOOK_SERVICE_VERSION, &services::HookApi() },
                { WXL_HOOK_RAW_SERVICE, WXL_HOOK_RAW_SERVICE_VERSION, &services::HookRawApi() },
                { WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION, &services::PatchApi() },
            };

            WLOG_INFO(kTag, "runtime %s (%s) version %u, abi %u.%u, target %s, mods in %ls",
                      kRuntimeId, kRuntimeName, static_cast<unsigned>(WXL_CORE_VERSION),
                      static_cast<unsigned>(WXL_ABI_VERSION_MAJOR),
                      static_cast<unsigned>(WXL_ABI_VERSION_MINOR), cfg.target.c_str(),
                      cfg.mods_directory.c_str());
            for (const wxl::host::PublishedService& service : cfg.services)
                WLOG_INFO(kTag, "service %s version %u", service.name.c_str(), service.version);

            g_host = new wxl::host::ModHost(std::move(cfg));

            // The core's own detours go in before any mod can attach behind them, and only in
            // the image whose addresses they are.
            if (!image::IsClient())
                WLOG_WARN(kTag, "the executable is not Wow.exe build %u: named hook points and "
                                "WorldEnter are unavailable", static_cast<unsigned>(image::kBuild));
            else if (!world::Install())
                WLOG_ERROR(kTag, "world seam not installed; WorldEnter will not be raised");

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
