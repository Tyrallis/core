// The x86 detour of a seam row, generated from its return type, convention and parameters.
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

#pragma once

#include "wxl/seams/seams.h"

#include <type_traits>

// x86 only: the conventions here are the stock client's. The harness includes it on Win32 to
// drive a generated detour against a function of the same shape.

namespace wxl::core::seams
{
    /**
     * @brief The detour of one seam row: Before, the original, After, in the row's convention.
     *
     * @tparam Conv       the row's convention tag (wxl::seam::detail::conv_*).
     * @tparam Signature  the row's `ret params` as a function type.
     *
     * Link<Before, After> carries the trampoline the hook engine fills (`original`) and the
     * function to attach (`Detour`). Arguments are forwarded by value, which is what the C
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
            static inline Fn original = nullptr;

            static R __cdecl Detour(A... args)
            {
                Before(args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(args...);
                    After(args...);
                }
                else
                {
                    R result = original(args...);
                    After(result, args...);
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
            static inline Fn original = nullptr;

            static R __stdcall Detour(A... args)
            {
                Before(args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(args...);
                    After(args...);
                }
                else
                {
                    R result = original(args...);
                    After(result, args...);
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
            static inline Fn original = nullptr;

            static R __fastcall Detour(This self, void*, A... args)
            {
                Before(self, args...);
                if constexpr (std::is_void_v<R>)
                {
                    original(self, nullptr, args...);
                    After(self, args...);
                }
                else
                {
                    R result = original(self, nullptr, args...);
                    After(result, self, args...);
                    return result;
                }
            }
        };
    };
}
