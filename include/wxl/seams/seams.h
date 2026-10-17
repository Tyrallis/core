// The seams as functions: Before and After of every row of the seam table.
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

#ifndef WXL_SEAMS_H
#define WXL_SEAMS_H

// Every seam of seams.def declared as a pair of functions, wxl::seam::<domain>::<name>::Before
// and ::After, taking the row's parameters. Both builds of the runtime define and export them;
// what differs is who calls them: on x86 the runtime's own detour at the function's address, on
// x64 the recompiled client from inside the function, which is why this header is C++ and the
// client includes it. A seam's body raises the events the call justifies, and nothing else.
//
// Before takes the row's parameters. After takes them too, and when the row returns a value,
// that value first: a row `WXL_SEAM(MapArea, PrepareChunk, ..., void*, thiscall, (void* self,
// int32_t x, int32_t y))` declares Before(self, x, y) and After(result, self, x, y). A thiscall
// row's first parameter is `this`.
//
// C++17: the recompiled client compiles this header with its own language level.

#include <stdint.h>

#include <type_traits>

#include "wxl/abi.h"
#include "wxl/core/init.h"

#ifndef __cplusplus
#error "wxl/seams/seams.h declares C++ functions; include it from C++"
#endif

namespace wxl::seam::detail
{
    /// The calling conventions a row may declare, by the token of its conv column.
    struct conv_cdecl
    {
    };
    struct conv_thiscall
    {
    };
    struct conv_stdcall
    {
    };
    /// A function the compiler gave a private register convention: the x86 build declares the seam
    /// and never attaches it, because no detour signature can describe how the arguments arrive.
    struct conv_regparm
    {
    };

    /// The types of Before and After for a row's signature: After takes a non-void result first.
    template <class Signature>
    struct Fn;

    template <class R, class... A>
    struct Fn<R(A...)>
    {
        using Before = void WXL_CALL(A...);
        using After  = void WXL_CALL(R, A...);
    };

    template <class... A>
    struct Fn<void(A...)>
    {
        using Before = void WXL_CALL(A...);
        using After  = void WXL_CALL(A...);
    };

    /// Whether a return type comes back in a register: nothing, or a scalar of at most 8 bytes.
    template <class R>
    struct RegisterReturn
    {
        static constexpr bool ok = std::is_scalar_v<R> && sizeof(R) <= 8;
    };

    template <>
    struct RegisterReturn<void>
    {
        static constexpr bool ok = true;
    };

    /// What the x86 detour can carry for a convention: a thiscall row takes `this` first, as a
    /// pointer, and returns nothing or a value that fits a register.
    template <class Conv, class Signature>
    struct Shape
    {
        static constexpr bool ok = true;
    };

    template <class R, class This, class... A>
    struct Shape<conv_thiscall, R(This, A...)>
    {
        static constexpr bool ok = std::is_pointer_v<This> && RegisterReturn<R>::ok;
    };

    template <class R>
    struct Shape<conv_thiscall, R()>
    {
        static constexpr bool ok = false;
    };
} // namespace wxl::seam::detail

namespace wxl::seam
{
#define WXL_SEAM(domain, name, symbol, ret, conv, params)                                       \
    namespace domain::name                                                                      \
    {                                                                                           \
        using Signature  = ret params;                                                          \
        using Convention = ::wxl::seam::detail::conv_##conv;                                    \
        static_assert(::wxl::seam::detail::Shape<Convention, Signature>::ok,                    \
                      "seam " #domain "." #name ": a thiscall row takes `this` first, as a "    \
                      "pointer, and returns void or a scalar");                                 \
        WXL_CORE_API ::wxl::seam::detail::Fn<Signature>::Before Before;                         \
        WXL_CORE_API ::wxl::seam::detail::Fn<Signature>::After After;                           \
    }
#include "wxl/seams/seams.def"
#undef WXL_SEAM
} // namespace wxl::seam

#endif // WXL_SEAMS_H
