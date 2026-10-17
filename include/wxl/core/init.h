// WarcraftXL.dll on x64: the entry point the client calls to bring the runtime up.
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

#ifndef WXL_CORE_INIT_H
#define WXL_CORE_INIT_H

// The recompiled client links WarcraftXL.dll and calls WarcraftXLInit once its process is up.
// The stock client never sees this header: there the DLL is started by the entry trampoline the
// patcher adds (WarcraftXLEntry), with nothing to hand over.
//
// C, not C++, like the SDK contract: the client includes it from whatever language level it is
// built at, and the struct is the only thing both sides have to agree on.

#include <stdint.h>

#include "wxl/abi.h"

#if defined(WXL_CORE_BUILD)
    #define WXL_CORE_API __declspec(dllexport)
#else
    #define WXL_CORE_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/// What the client hands the runtime. Zero it, set struct_size to sizeof(wxl_core_init); a later
/// version of this header appends fields and the runtime reads only what struct_size covers.
typedef struct wxl_core_init
{
    uint32_t struct_size;
} wxl_core_init;

/**
 * @brief Brings the runtime up: the mod host, every mod under Mods\ next to the client
 *        executable, then WXL_STAGE_EARLY.
 *
 * Call once, on the main thread, before the client reads anything a mod may want to change, and
 * never under the loader lock: loading a mod is a LoadLibrary. The host lives for the rest of the
 * process. The log is Logs\WarcraftXL.log next to the client executable, open since DllMain.
 *
 * @return WXL_OK when the host is up and its mods are loaded; WXL_E_INVALID when init is null or
 *         its struct_size is too small; WXL_E_FAILED when the host could not be stood up, in which
 *         case the reason is in the log and the client goes on without mods. A second call
 *         returns the outcome of the first.
 */
WXL_CORE_API wxl_result WXL_CALL WarcraftXLInit(const wxl_core_init* init);

#ifdef __cplusplus
}
#endif

#endif // WXL_CORE_INIT_H
