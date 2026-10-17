// DLL entry on x86: the two exports the patched client imports, and DllMain, which only opens the log.
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
#include "runtime/Image.hpp"

#include <windows.h>

/**
 * @brief IAT anchor; the patcher imports this symbol so the loader maps the DLL with the client.
 */
extern "C" __declspec(dllexport) void __cdecl WarcraftXL(void) {}

/**
 * @brief The entry-point trampoline's call: runs on the client's main thread, after the loader
 *        has initialised every static import and before the client's own entry point.
 *
 * The loader lock is not held here, so loading mods is safe, and nothing of the client has run
 * yet, so a mod that patches what the client reads at start-up gets there first.
 */
extern "C" __declspec(dllexport) void __cdecl WarcraftXLEntry(void)
{
    WLOG_INFO(wxl::core::kTag, "entry trampoline on thread %lu, before the client's entry point",
              GetCurrentThreadId());
    wxl::core::Start();
}

namespace
{
    /**
     * @brief The core's thread, for a client without the entry trampoline: loads the mods once
     *        the process is past its loader lock, while the client boots in parallel.
     * @return thread exit code (0).
     */
    DWORD WINAPI CoreThread(LPVOID)
    {
        WLOG_INFO(wxl::core::kTag, "core thread %lu up", GetCurrentThreadId());
        wxl::core::Start();
        return 0;
    }
}

/**
 * @brief DLL entry point; on process attach opens the log and, when the executable carries no
 *        entry trampoline, spawns the core thread.
 *
 * Nothing here takes a lock the loader could hold, loads a library, or runs code of another DLL:
 * kernel32 calls only, then the loader thread is handed back.
 * @param module  DLL module handle.
 * @param reason  reason code for the call.
 * @return TRUE to keep the DLL loaded.
 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
    {
        DisableThreadLibraryCalls(module);
        wxl::core::OpenLog();
        WLOG_INFO(wxl::core::kTag, "WarcraftXL.dll starting (build %s %s), loader thread %lu",
                  __DATE__, __TIME__, GetCurrentThreadId());

        if (wxl::core::image::HasEntryTrampoline())
        {
            WLOG_INFO(wxl::core::kTag, "entry trampoline present; mods load at the entry point");
            break;
        }

        WLOG_WARN(wxl::core::kTag, "the executable was patched by an older patcher (import only, "
                                   "no entry trampoline): mods load on a core thread while the "
                                   "client boots, and WXL_STAGE_EARLY runs late. Run "
                                   "wxl-core-patcher on the executable to add the trampoline.");
        HANDLE thread = CreateThread(nullptr, 0, &CoreThread, nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
        else WLOG_ERROR(wxl::core::kTag, "cannot create the core thread (win32=%lu); no mods loaded",
                        GetLastError());
        break;
    }
    case DLL_PROCESS_DETACH:
        WLOG_INFO(wxl::core::kTag, "WarcraftXL.dll detaching");
        wxl::log::Close();
        break;
    default:
        break;
    }
    return TRUE;
}
