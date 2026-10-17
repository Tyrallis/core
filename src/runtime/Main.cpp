// DLL entry: open the log on the loader thread, then stand the mod host up on a thread of its own.
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

#include <windows.h>

/**
 * @brief IAT anchor; the patcher imports this symbol so the loader maps the DLL with the client.
 */
extern "C" __declspec(dllexport) void __cdecl WarcraftXL(void) {}

namespace
{
    /**
     * @brief The core's thread: loads the mods once the process is past its loader lock.
     *
     * A thread created from DllMain cannot start before the loader lock is released, which is
     * after every static import, this DLL included, has been initialised. Loading a mod is a
     * LoadLibrary, which would deadlock under that lock, so it happens here and nowhere earlier.
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
 * @brief DLL entry point; on process attach opens the log and spawns the core thread.
 *
 * Nothing here takes a lock the loader could hold, loads a library, or runs code of another DLL:
 * kernel32 calls only, then the thread handle is dropped and the loader thread is handed back.
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
