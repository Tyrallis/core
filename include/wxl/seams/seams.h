// The seams as functions: Before and After of every row of the seam table.
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

#ifndef WXL_SEAMS_H
#define WXL_SEAMS_H

// Every seam of seams.def declared as a pair of functions, wxl::seam::<domain>::<name>::Before
// and ::After, taking the row's parameters. Both builds of the runtime define and export them;
// what differs is who calls them: on x86 the runtime's own detour at the function's address, on
// x64 the recompiled client from inside the function, which is why this header is C++ and the
// client includes it. A seam's body raises the events the call justifies, and nothing else.

#include <stdint.h>

#include "wxl/abi.h"
#include "wxl/core/init.h"

#ifndef __cplusplus
#error "wxl/seams/seams.h declares C++ functions; include it from C++"
#endif

namespace wxl::seam
{
#define WXL_SEAM(domain, name, symbol, params) \
    namespace domain::name                     \
    {                                          \
        WXL_CORE_API void WXL_CALL Before params; \
        WXL_CORE_API void WXL_CALL After params;  \
    }
#include "wxl/seams/seams.def"
#undef WXL_SEAM
}

#endif // WXL_SEAMS_H
