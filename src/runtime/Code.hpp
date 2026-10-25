// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The code the client accepts: the address ranges whose functions the client may be handed.

#pragma once

#include "wxl/host/loader.hpp"

#include <cstdint>

namespace wxl::core::code
{
    /// The tag of the registry's lines.
    constexpr const char* kTag = "code";

    /**
     * @brief Registers the executable sections of a module mapped in the process.
     *
     * The client checks every function it is about to call through a pointer it was handed (a C
     * function its Lua calls, a Lua hook, a console command's handler, a CVar's callback) with
     * OsValidateFunctionPointer, which ends the process unless the pointer is in its own .text.
     * The Os.ValidateFunctionPointer seam lets through a pointer inside a range registered here,
     * so a function of the runtime or of a mod's binary may be handed to the client.
     *
     * @param module  the module's base address (its HMODULE). A module registered twice keeps
     *                one set of ranges.
     * @return false when the module's headers cannot be read or the table is full.
     */
    bool AddModule(const void* module);

    /// Forgets the ranges of a module: called before it is unmapped. Unknown is no mistake.
    void RemoveModule(const void* module);

    /// Registers the runtime's own image, WarcraftXL.dll.
    bool AddOwnImage();

    /**
     * @brief Whether @p pointer lies in a registered range.
     *
     * Runs on every call the client's Lua makes into C, on whichever thread makes it: no lock,
     * no allocation. A pointer outside every registered module is told by two compares.
     */
    bool Accepts(const void* pointer);

    /**
     * @brief The loader the mod host opens binaries with: the platform's, registering each
     *        binary's code once it is mapped and forgetting it before it is unmapped.
     *
     * A binary the host abandons (one whose code faulted) is never closed and stays registered,
     * since it stays mapped.
     */
    wxl::host::LibraryLoader& HostLoader();

    /**
     * @brief Whether a function the runtime is about to hand the client is code the client will
     *        call, logging an error naming @p what when it is not.
     *
     * Every native function pushed into the client's Lua, console or CVar tables goes through
     * this first: a pointer it refuses would end the client at its first call.
     */
    bool Handable(const void* function, const char* what);

    /// Forgets every range: the harness, between its checks.
    void Reset();
}
