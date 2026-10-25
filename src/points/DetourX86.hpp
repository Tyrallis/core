// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The x86 detour of a point row: the arguments by address into points::Enter, and the original
// called back through the hook chain.

#pragma once

#include "points/Points.hpp"

#include "wxl/seams/seams.h"

#include <cstddef>
#include <type_traits>
#include <utility>

// x86 only: the conventions are the stock client's. The harness includes it on Win32 to drive a
// generated detour against a function of the same shape.

namespace wxl::core::points
{
    /**
     * @brief The detour of point @p Index: a function of the point's convention and signature
     *        that hands Enter the addresses of its own parameters, and the function Enter calls
     *        as the original, which calls the next link of the address's chain with them.
     *
     * `original` is the chain's to fill (hook::Attach). The parameters are the detour's own, so
     * a handler that writes through args[i] changes what the original is called with.
     */
    template <uint32_t Index, class Conv, class Signature>
    struct DetourOf;

    namespace detail
    {
        template <class R, class Fn, class... A, size_t... I>
        void CallWith(Fn fn, void* const* args, void* result, std::index_sequence<I...>)
        {
            if constexpr (std::is_void_v<R>)
            {
                (void)result;
                fn(*static_cast<A*>(args[I])...);
            }
            else
            {
                *static_cast<R*>(result) = fn(*static_cast<A*>(args[I])...);
            }
        }

        template <uint32_t Index, class R, class... A>
        R Enter(A&... args, Original original)
        {
            void* addresses[sizeof...(A) ? sizeof...(A) : 1] = {&args...};
            if constexpr (std::is_void_v<R>)
            {
                points::Enter(Index, addresses, nullptr, original);
            }
            else
            {
                R result{};
                points::Enter(Index, addresses, &result, original);
                return result;
            }
        }
    }

    template <uint32_t Index, class R, class... A>
    struct DetourOf<Index, ::wxl::seam::detail::conv_cdecl, R(A...)>
    {
        using Fn = R(__cdecl*)(A...);

        static inline Fn original = nullptr;

        static void WXL_CALL Original(void* const* args, void* result)
        {
            detail::CallWith<R, Fn, A...>(original, args, result, std::index_sequence_for<A...>{});
        }

        static R __cdecl Detour(A... args) { return detail::Enter<Index, R, A...>(args..., &Original); }
    };

    template <uint32_t Index, class R, class... A>
    struct DetourOf<Index, ::wxl::seam::detail::conv_stdcall, R(A...)>
    {
        using Fn = R(__stdcall*)(A...);

        static inline Fn original = nullptr;

        static void WXL_CALL Original(void* const* args, void* result)
        {
            detail::CallWith<R, Fn, A...>(original, args, result, std::index_sequence_for<A...>{});
        }

        static R __stdcall Detour(A... args) { return detail::Enter<Index, R, A...>(args..., &Original); }
    };

    // A __thiscall function takes `this` in ECX and pops its stack arguments, as a __fastcall
    // function with a dummy second parameter (EDX) does: the detour is that, and the original is
    // called the same way with the dummy left null (as seams/Detour.hpp does).
    template <uint32_t Index, class R, class This, class... A>
    struct DetourOf<Index, ::wxl::seam::detail::conv_thiscall, R(This, A...)>
    {
        using Fn = R(__fastcall*)(This, void*, A...);

        static inline Fn original = nullptr;

        template <size_t... I>
        static void Call(void* const* args, void* result, std::index_sequence<I...>)
        {
            if constexpr (std::is_void_v<R>)
            {
                (void)result;
                original(*static_cast<This*>(args[0]), nullptr, *static_cast<A*>(args[I + 1])...);
            }
            else
            {
                *static_cast<R*>(result) = original(*static_cast<This*>(args[0]), nullptr, *static_cast<A*>(args[I + 1])...);
            }
        }

        static void WXL_CALL Original(void* const* args, void* result)
        {
            Call(args, result, std::index_sequence_for<A...>{});
        }

        static R __fastcall Detour(This self, void*, A... args)
        {
            return detail::Enter<Index, R, This, A...>(self, args..., &Original);
        }
    };
}
