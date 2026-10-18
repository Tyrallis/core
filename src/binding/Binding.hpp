// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The client binding as functions: one per row of wxl/core/binding.def, the same on both builds.

#pragma once

#include "wxl/abi.h"
#include "wxl/core/init.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wxl::core::binding
{
    // Every row of binding.def as a function. A seam body or an object read calls these and
    // nothing else to learn about the client. Who defines them is the attachment: BindingX86.cpp
    // reads the client's memory at measured offsets, BindingTable.cpp forwards to the accessors
    // of a wxl_core_init (the x64 client's, or the harness's).
#define WXL_BINDING(name, ret, params, args, absent) ret name params;
#include "wxl/core/binding.def"
#undef WXL_BINDING

    /**
     * @brief Whether a wxl_core_init was built from the same binding table as this runtime.
     *
     * Compares its rows with wxl_core_init_rows, one by one, as far as both reach: a client with
     * fewer rows is older and agrees, one with more is newer and agrees. A row that differs
     * means the accessors after it are not where this runtime would call them.
     * @param why  receives the reason when the answer is false.
     */
    bool Agrees(const wxl_core_init& init, char* why, size_t capacity);

    /**
     * @brief Keeps the accessors of a wxl_core_init, as far as its struct_size reaches.
     *
     * Table-filled builds only (BindingTable.cpp): on x64 WarcraftXLInit calls it with what the
     * client handed over, in the harness the test calls it with its fakes. A row the table did
     * not reach, or left null, answers its absent value.
     */
    void Bind(const wxl_core_init& init);

    /// The rows the bound table left null, by name; empty when every fact is readable.
    std::vector<const char*> Unbound();
}
