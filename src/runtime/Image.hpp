// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The process image: whether it is the client build the offset tables describe, and whether the
// patcher gave it the entry trampoline.

#pragma once

#include <atomic>
#include <cstdint>

namespace wxl::core::image
{
    /// The client build every address in the offset tables belongs to.
    constexpr uint32_t kBuild = 12340;

    namespace detail
    {
        /// Unknown until the headers have been read: -1, then 0 or 1. Never read directly.
        extern std::atomic<signed char> g_isClient;

        /// Reads the PE headers and decides. Called once; IsClient keeps what it said.
        bool ReadIsClient();
    }

    /**
     * @brief Whether the process executable is Wow.exe build 12340.
     *
     * Decided on the PE header's link timestamp, which the patcher does not touch: the named
     * hook points and the core's own detours are armed only when this holds, since their
     * addresses mean nothing in any other image.
     *
     * Every read of the client binding asks this first, which puts it on the per-packet and
     * per-object paths, so the headers are read once and the answer kept. It is inline for
     * the same reason: the call itself is most of what is left. The image cannot change while
     * the process runs, so two threads racing to be first compute the same value and the
     * store is ordered against nothing.
     */
    inline bool IsClient()
    {
        signed char known = detail::g_isClient.load(std::memory_order_relaxed);
        if (known < 0)
        {
            known = detail::ReadIsClient() ? 1 : 0;
            detail::g_isClient.store(known, std::memory_order_relaxed);
        }
        return known != 0;
    }

    /// kBuild when IsClient, else 0.
    uint32_t Build();

    /**
     * @brief Whether the client executable carries the patcher's entry-point trampoline, which
     *        calls WarcraftXLEntry before the client's own entry point.
     *
     * Reads the executable's headers in memory: safe under the loader lock.
     */
    bool HasEntryTrampoline();
}
