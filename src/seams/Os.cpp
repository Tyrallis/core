// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Os seam: the client's check of the function pointers it is handed.

#include "wxl/seams/seams.h"

#include "runtime/Code.hpp"

namespace wxl::seam::Os::ValidateFunctionPointer
{
    bool WXL_CALL Gate(const void** ptr)
    {
        return wxl::core::code::Accepts(*ptr);
    }
}
