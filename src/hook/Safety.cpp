// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// SafetyHook and Zydis behind a plain interface (see Safety.hpp). Built as C++23 with them.

#include "hook/Safety.hpp"

#include <safetyhook.hpp>
#include <Zydis/Zydis.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>

namespace wxl::hook::safety
{
    struct Inline
    {
        safetyhook::InlineHook hook;
    };

    struct Mid
    {
        safetyhook::MidHook hook;
    };

    static_assert(sizeof(Registers) == sizeof(safetyhook::Context), "Registers is SafetyHook's x86 context");
    static_assert(offsetof(Registers, eflags) == offsetof(safetyhook::Context, eflags) &&
                      offsetof(Registers, ecx) == offsetof(safetyhook::Context, ecx) &&
                      offsetof(Registers, esp) == offsetof(safetyhook::Context, esp) &&
                      offsetof(Registers, eip) == offsetof(safetyhook::Context, eip),
                  "Registers is SafetyHook's x86 context, member for member");

    namespace
    {
        constexpr size_t kJump = 5; ///< what a detour writes: jmp rel32

        void Say(char* why, size_t capacity, const char* format, ...)
        {
            if (!why || !capacity) return;
            va_list args;
            va_start(args, format);
            std::vsnprintf(why, capacity, format, args);
            va_end(args);
        }

        const char* InlineError(const safetyhook::InlineHook::Error& error)
        {
            using Type = safetyhook::InlineHook::Error;
            switch (error.type)
            {
            case Type::BAD_ALLOCATION: return "no memory near the function for its trampoline";
            case Type::FAILED_TO_DECODE_INSTRUCTION: return "an instruction of its first bytes cannot be decoded";
            case Type::SHORT_JUMP_IN_TRAMPOLINE: return "a short jump in its first bytes cannot be moved to the trampoline";
            case Type::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE: return "an instruction of its first bytes addresses something out of reach once moved";
            case Type::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE: return "an instruction of its first bytes cannot be moved to a trampoline";
            case Type::FAILED_TO_UNPROTECT: return "its bytes could not be made writable";
            case Type::NOT_ENOUGH_SPACE: return "the function is too short for a jump";
            }
            return "SafetyHook refused it";
        }
    }

    bool CheckPrologue(const void* target, size_t length, char* why, size_t capacity)
    {
        ZydisDecoder decoder;
        ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LEGACY_32, ZYDIS_STACK_WIDTH_32);
        const uint8_t* const bytes = static_cast<const uint8_t*>(target);

        if (bytes[0] == 0xE9 || bytes[0] == 0xEB || (bytes[0] == 0xFF && bytes[1] == 0x25))
        {
            Say(why, capacity, "its first instruction is already a jump (%02X %02X): another tool has detoured it",
                bytes[0], bytes[1]);
            return false;
        }

        // The instructions the jump covers.
        size_t covered = 0;
        while (covered < kJump)
        {
            ZydisDecodedInstruction insn;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, bytes + covered, 15, &insn)))
            {
                Say(why, capacity, "the instruction at +%zu cannot be decoded", covered);
                return false;
            }
            const bool ends = insn.mnemonic == ZYDIS_MNEMONIC_RET || insn.mnemonic == ZYDIS_MNEMONIC_INT3 ||
                              insn.mnemonic == ZYDIS_MNEMONIC_JMP;
            covered += insn.length;
            if (ends && covered < kJump)
            {
                Say(why, capacity, "the function ends after %zu byte(s), before the %zu a jump takes", covered, kJump);
                return false;
            }
        }
        if (length && length < kJump)
        {
            Say(why, capacity, "the function is %zu byte(s), too short for a jump", length);
            return false;
        }

        // No branch of the body may land inside the bytes the jump replaces.
        for (size_t at = 0; length && at < length;)
        {
            ZydisDecodedInstruction insn;
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeInstruction(&decoder, nullptr, bytes + at, 15, &insn)))
            {
                ++at; // data or padding inside the range: not code a branch comes from
                continue;
            }
            if ((insn.attributes & ZYDIS_ATTRIB_IS_RELATIVE) &&
                (insn.meta.category == ZYDIS_CATEGORY_COND_BR || insn.meta.category == ZYDIS_CATEGORY_UNCOND_BR))
            {
                const intptr_t displacement = static_cast<intptr_t>(insn.raw.imm[0].value.s);
                const intptr_t landing      = static_cast<intptr_t>(at + insn.length) + displacement;
                if (landing > 0 && static_cast<size_t>(landing) < covered)
                {
                    Say(why, capacity, "the branch at +0x%zX lands at +%zd, inside the %zu byte(s) the jump replaces",
                        at, landing, covered);
                    return false;
                }
            }
            at += insn.length;
        }
        return true;
    }

    Inline* Create(void* target, size_t length, void* destination, void** trampoline, char* why, size_t capacity)
    {
        if (!CheckPrologue(target, length, why, capacity)) return nullptr;
        auto made = safetyhook::InlineHook::create(target, destination, safetyhook::InlineHook::StartDisabled);
        if (!made)
        {
            Say(why, capacity, "%s", InlineError(made.error()));
            return nullptr;
        }
        Inline* detour = new (std::nothrow) Inline{std::move(*made)};
        if (!detour)
        {
            Say(why, capacity, "out of memory");
            return nullptr;
        }
        *trampoline = detour->hook.original<void*>();
        return detour;
    }

    bool Arm(Inline* detour, char* why, size_t capacity)
    {
        if (!detour) return false;
        if (detour->hook.enabled()) return true;
        if (auto done = detour->hook.enable(); !done)
        {
            Say(why, capacity, "%s", InlineError(done.error()));
            return false;
        }
        return true;
    }

    bool Disarm(Inline* detour, char* why, size_t capacity)
    {
        if (!detour) return false;
        if (!detour->hook.enabled()) return true;
        if (auto done = detour->hook.disable(); !done)
        {
            Say(why, capacity, "%s", InlineError(done.error()));
            return false;
        }
        return true;
    }

    Mid* CreateMid(void* target, MidFn fn, char* why, size_t capacity)
    {
        if (!CheckPrologue(target, 0, why, capacity)) return nullptr;
        auto made = safetyhook::MidHook::create(target, reinterpret_cast<safetyhook::MidHookFn>(fn),
                                                safetyhook::MidHook::StartDisabled);
        if (!made)
        {
            Say(why, capacity, "SafetyHook refused a mid-function hook there (error %d)", static_cast<int>(made.error().type));
            return nullptr;
        }
        Mid* hook = new (std::nothrow) Mid{std::move(*made)};
        if (!hook) Say(why, capacity, "out of memory");
        return hook;
    }

    bool ArmMid(Mid* hook, char* why, size_t capacity)
    {
        if (!hook) return false;
        if (hook->hook.enabled()) return true;
        if (auto done = hook->hook.enable(); !done)
        {
            Say(why, capacity, "SafetyHook could not arm the mid-function hook");
            return false;
        }
        return true;
    }

    bool DisarmMid(Mid* hook, char* why, size_t capacity)
    {
        if (!hook) return false;
        if (!hook->hook.enabled()) return true;
        if (auto done = hook->hook.disable(); !done)
        {
            Say(why, capacity, "SafetyHook could not disarm the mid-function hook");
            return false;
        }
        return true;
    }
}
