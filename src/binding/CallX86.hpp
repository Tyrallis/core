// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Calling the client's own functions from the x86 binding, by the conventions Wow.exe gives them.

#pragma once

#include <cstdint>

namespace wxl::core::binding::x86
{
    /**
     * @brief Calls a __thiscall member of the client at an address.
     *
     * The callee takes `this` in ECX and its arguments on the stack, and pops them (`ret 4n`).
     * A __fastcall pointer does the same with a second, dummy argument in EDX: its first two
     * arguments go in ECX and EDX, every other on the stack, popped by the callee. An argument
     * of 64 bits (a WOWGUID passed by value) takes two stack slots, low first, as the client's
     * callers push it. A bool answer is AL alone, which a `bool` R reads.
     */
    template <class R, class... A>
    R CallMember(std::uintptr_t site, const void* self, A... args)
    {
        using Fn = R(__fastcall*)(const void*, void*, A...);
        return reinterpret_cast<Fn>(site)(self, nullptr, args...);
    }

    /// Calls a free function of the client at an address: cdecl, the caller pops its arguments.
    template <class R, class... A>
    R CallFree(std::uintptr_t site, A... args)
    {
        using Fn = R(__cdecl*)(A...);
        return reinterpret_cast<Fn>(site)(args...);
    }
}
