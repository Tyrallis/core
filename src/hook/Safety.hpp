// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// SafetyHook and Zydis behind a plain interface: inline detours armed and disarmed in place,
// mid-function hooks, and the check of a function's first bytes before any of them is written.

#pragma once

#include <cstddef>
#include <cstdint>

// SafetyHook needs C++23 (std::expected); the runtime is C++20. Only Safety.cpp, built with
// SafetyHook into its own library, sees SafetyHook's headers; everything else goes through this.
// x86 only: the recompiled client is never detoured.

namespace wxl::hook::safety
{
    /// An inline detour of SafetyHook's: the target's first instructions moved to a trampoline
    /// and replaced by a jump. Made disarmed; Arm writes the jump, Disarm puts the bytes back.
    /// The trampoline stays for the process's life, so a call still in it when the detour is
    /// disarmed completes.
    struct Inline;

    /**
     * @brief Makes a detour of @p target to @p destination, disarmed.
     *
     * The target's first bytes are checked first (CheckPrologue). @p trampoline receives the
     * function that runs the original.
     * @return the detour, or null with the reason in @p why.
     */
    Inline* Create(void* target, size_t length, void* destination, void** trampoline, char* why,
                   size_t capacity);

    /// Writes the jump, with every other thread held off the bytes it covers.
    bool Arm(Inline* detour, char* why, size_t capacity);

    /// Puts the original bytes back, the same way.
    bool Disarm(Inline* detour, char* why, size_t capacity);

    /**
     * @brief The registers at a mid-function hook, as SafetyHook's x86 context lays them out.
     * A write to one is what the code at the hooked address sees when it goes on.
     */
    struct Registers
    {
        alignas(8) uint8_t xmm[8][16];
        uintptr_t eflags, edi, esi, edx, ecx, ebx, eax, ebp, esp, trampoline_esp, eip;
    };

    using MidFn = void (*)(Registers& registers);

    /// A mid-function hook: @p fn runs whenever execution reaches the address, then the code
    /// there goes on. Made disarmed.
    struct Mid;

    Mid* CreateMid(void* target, MidFn fn, char* why, size_t capacity);

    bool ArmMid(Mid* hook, char* why, size_t capacity);
    bool DisarmMid(Mid* hook, char* why, size_t capacity);

    /**
     * @brief Whether a detour may overwrite the first five bytes of a function, read by Zydis.
     *
     * Refused, with the reason in @p why: bytes Zydis cannot decode; a function that ends (ret,
     * jmp, int3) inside those bytes; a function already starting with a jump another tool wrote;
     * a branch of the function (its @p length bytes, when known) that lands inside them, which
     * would land in the middle of the jump.
     */
    bool CheckPrologue(const void* target, size_t length, char* why, size_t capacity);
}
