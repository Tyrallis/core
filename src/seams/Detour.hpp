// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The x86 detour of a seam row, generated from its return type, convention and parameters.

#pragma once

#include "wxl/seams/seams.h"

#include <type_traits>

// x86 only: the conventions here are the stock client's. The harness includes it on Win32 to
// drive a generated detour against a function of the same shape.

namespace wxl::core::seams
{
    /// Calls one side of an observing row; a side the row does not declare is passed as nullptr.
    template <auto Fn, class... T>
    inline void Side(T... args)
    {
        if constexpr (!std::is_null_pointer_v<decltype(Fn)>) Fn(args...);
    }

    /**
     * @brief The detour of one seam row: Before, the original, After, in the row's convention.
     *
     * @tparam Conv       the row's convention tag (wxl::seam::detail::conv_*).
     * @tparam Signature  the row's `ret params` as a function type.
     *
     * Link<Before, After> carries the trampoline the hook engine fills (`original`) and the
     * function to attach (`Detour`); a one-sided row passes nullptr for the side it lacks. Arguments are forwarded by value, which is what the C
     * boundary passes anyway; a non-void result is handed to After first and returned.
     */
    template <class Conv, class Signature>
    struct DetourOf;

    template <class R, class... A>
    struct DetourOf<::wxl::seam::detail::conv_cdecl, R(A...)>
    {
        using Fn = R(__cdecl*)(A...);

        template <auto Before, auto After>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __cdecl Detour(A... args)
            {
                Side<Before>(args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(args...);
                    Side<After>(args...);
                }
                else
                {
                    R result = original(args...);
                    Side<After>(result, args...);
                    return result;
                }
            }
        };
    };

    template <class R, class... A>
    struct DetourOf<::wxl::seam::detail::conv_stdcall, R(A...)>
    {
        using Fn = R(__stdcall*)(A...);

        template <auto Before, auto After>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __stdcall Detour(A... args)
            {
                Side<Before>(args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(args...);
                    Side<After>(args...);
                }
                else
                {
                    R result = original(args...);
                    Side<After>(result, args...);
                    return result;
                }
            }
        };
    };

    // A __thiscall function takes `this` in ECX and its arguments on the stack, which it pops.
    // A __fastcall function with a dummy second parameter is called the same way: `this` in
    // ECX, the dummy in EDX, the rest on the stack, callee-popped. So the detour is __fastcall,
    // and the original is called through a __fastcall pointer with the dummy left null.
    template <class R, class This, class... A>
    struct DetourOf<::wxl::seam::detail::conv_thiscall, R(This, A...)>
    {
        using Fn = R(__fastcall*)(This, void*, A...);

        template <auto Before, auto After>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __fastcall Detour(This self, void*, A... args)
            {
                Side<Before>(self, args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(self, nullptr, args...);
                    Side<After>(self, args...);
                }
                else
                {
                    R result = original(self, nullptr, args...);
                    Side<After>(result, self, args...);
                    return result;
                }
            }
        };
    };

    /**
     * @brief The generated half of a regparm row: none.
     *
     * No signature says which registers the arguments arrive in, so nothing here may be attached:
     * the row is attached through its thunk in RegparmX86.cpp. This only keeps the table uniform.
     */
    template <class R, class... A>
    struct DetourOf<::wxl::seam::detail::conv_regparm, R(A...)>
    {
        using Fn = R(__cdecl*)(A...);

        template <auto Before, auto After>
        struct Link
        {
            static constexpr bool kAttachable = false;

            static inline Fn original = nullptr;

            static R __cdecl Detour(A... args)
            {
                ((void)args, ...);
                if constexpr (!std::is_void_v<R>) return R{};
            }
        };
    };

    /**
     * @brief The detour of one gated seam row: Gate decides, the original runs or does not.
     *
     * @tparam Conv       the row's convention tag (wxl::seam::detail::conv_*).
     * @tparam Signature  the row's `ret params` as a function type.
     *
     * The arguments are the detour's own parameters, so their addresses are what Gate is given:
     * a seam that writes through one rewrites what the original is then called with. A Gate
     * that takes the call leaves the result it wrote, and the original never runs.
     */
    template <class Conv, class Signature>
    struct GateOf;

    template <class R, class... A>
    struct GateOf<::wxl::seam::detail::conv_cdecl, R(A...)>
    {
        using Fn = R(__cdecl*)(A...);

        template <auto Gate>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __cdecl Detour(A... args)
            {
                if constexpr (std::is_void_v<R>)
                {
                    if (!Gate(&args...)) original(args...);
                }
                else
                {
                    R result{};
                    if (!Gate(&result, &args...)) result = original(args...);
                    return result;
                }
            }
        };
    };

    template <class R, class... A>
    struct GateOf<::wxl::seam::detail::conv_stdcall, R(A...)>
    {
        using Fn = R(__stdcall*)(A...);

        template <auto Gate>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __stdcall Detour(A... args)
            {
                if constexpr (std::is_void_v<R>)
                {
                    if (!Gate(&args...)) original(args...);
                }
                else
                {
                    R result{};
                    if (!Gate(&result, &args...)) result = original(args...);
                    return result;
                }
            }
        };
    };

    // The __fastcall shim of a __thiscall function, as above; the object crosses by address
    // like every other argument, so the shape is the same in every convention.
    template <class R, class This, class... A>
    struct GateOf<::wxl::seam::detail::conv_thiscall, R(This, A...)>
    {
        using Fn = R(__fastcall*)(This, void*, A...);

        template <auto Gate>
        struct Link
        {
            static constexpr bool kAttachable = true;

            static inline Fn original = nullptr;

            static R __fastcall Detour(This self, void*, A... args)
            {
                if constexpr (std::is_void_v<R>)
                {
                    if (!Gate(&self, &args...)) original(self, nullptr, args...);
                }
                else
                {
                    R result{};
                    if (!Gate(&result, &self, &args...)) result = original(self, nullptr, args...);
                    return result;
                }
            }
        };
    };

    /// The same for a gated row.
    template <class R, class... A>
    struct GateOf<::wxl::seam::detail::conv_regparm, R(A...)>
    {
        using Fn = R(__cdecl*)(A...);

        template <auto Gate>
        struct Link
        {
            static constexpr bool kAttachable = false;

            static inline Fn original = nullptr;

            static R __cdecl Detour(A... args)
            {
                ((void)args, ...);
                if constexpr (!std::is_void_v<R>) return R{};
            }
        };
    };
}
