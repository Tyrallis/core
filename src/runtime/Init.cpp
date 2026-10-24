// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// WarcraftXL.dll on x64: the entry point the recompiled client calls, and the DLL's own entry.

#include "wxl/core/init.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Stamp.hpp"

#include <windows.h>
#include <cstddef>

namespace
{
    /// The header every client hands over: struct_size and its rows. A client built against
    /// any version of the header from that one on is accepted; the accessors it did not have
    /// are read as absent.
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

    WLOG_INFO(wxl::core::kTag, "WarcraftXLInit on thread %lu (struct_size %u of %u, %u rows of %u)",
              GetCurrentThreadId(), init->struct_size, static_cast<unsigned>(sizeof(wxl_core_init)),
              init->row_count, WXL_CORE_INIT_ROW_COUNT);

    char why[512];
    if (!wxl::core::binding::Agrees(*init, why, sizeof why))
    {
        WLOG_ERROR(wxl::core::kTag, "WarcraftXLInit: the client and this runtime were built from "
                                    "different binding tables, so no mods are loaded: %s. "
                                    "Rebuild both from one tree.", why);
        return WXL_E_INVALID;
    }
    wxl::core::binding::Bind(*init);
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
    {
        DisableThreadLibraryCalls(module);
        wxl::core::OpenLog();
        // The DLL's own link stamp, not a compile time: it moves whenever this binary is
        // relinked, so the line names the file that is running.
        char linked[wxl::core::stamp::kDescriptionSize];
        wxl::core::stamp::Describe(linked, sizeof linked);
        WLOG_INFO(wxl::core::kTag, "WarcraftXL.dll starting (linked %s), loader thread %lu; "
                                   "mods load at WarcraftXLInit",
                  linked, GetCurrentThreadId());
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
