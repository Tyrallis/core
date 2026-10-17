// DLL entry on x64: DllMain opens the log; WarcraftXLInit, called by the client, starts the runtime.
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

#include "wxl/core/init.h"

#include "common/Log.hpp"
#include "runtime/AttachmentX64.hpp"
#include "runtime/Core.hpp"

#include <windows.h>
#include <cstddef>

namespace
{
    /// The smallest wxl_core_init ever published: struct_size alone. A client built against
    /// any version of the header from that one on is accepted; the fields it did not have are
    /// read as absent.
    constexpr uint32_t kMinInitSize = offsetof(wxl_core_init, map_id);
}

wxl_result WXL_CALL WarcraftXLInit(const wxl_core_init* init)
{
    if (!init || init->struct_size < kMinInitSize)
    {
        WLOG_ERROR(wxl::core::kTag, "WarcraftXLInit: wxl_core_init is %s (struct_size %u, need %u)",
                   init ? "too small" : "null", init ? init->struct_size : 0u,
                   static_cast<unsigned>(kMinInitSize));
        return WXL_E_INVALID;
    }

    WLOG_INFO(wxl::core::kTag, "WarcraftXLInit on thread %lu (struct_size %u)",
              GetCurrentThreadId(), init->struct_size);
    wxl::core::attachment::Bind(*init);
    return wxl::core::Start() ? WXL_OK : WXL_E_FAILED;
}

/**
 * @brief DLL entry point; on process attach opens the log, on detach closes it.
 *
 * The client links the DLL, so this runs under the loader lock before the client's own entry
 * point: kernel32 calls only, nothing loaded, no other DLL's code run. Mods wait for
 * WarcraftXLInit.
 * @param module  DLL module handle.
 * @param reason  reason code for the call.
 * @return TRUE to keep the DLL loaded.
 */
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        wxl::core::OpenLog();
        WLOG_INFO(wxl::core::kTag, "WarcraftXL.dll starting (build %s %s), loader thread %lu; "
                                   "mods load at WarcraftXLInit",
                  __DATE__, __TIME__, GetCurrentThreadId());
        break;
    case DLL_PROCESS_DETACH:
        WLOG_INFO(wxl::core::kTag, "WarcraftXL.dll detaching");
        wxl::log::Close();
        break;
    default:
        break;
    }
    return TRUE;
}
