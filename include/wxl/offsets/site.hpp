// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// Compile-time resolution of a site inside a client function: symbol + delta, checked.

#pragma once

// A mid-function address (a return address, a branch to patch, an immediate inside a body) is
// written as the function's name plus a byte delta, and resolved while compiling:
//
//     // The call site inside CGWorldFrame::HitTest where the result is written back.
//     constexpr uintptr_t kHitTestWriteback = WXL_SITE("CGWorldFrame::HitTest", 0x3C);
//
// The build fails, with a message naming the symbol, when the name is not in functions.def,
// when it is there under several addresses (an overload or a template instantiation: use
// WXL_SITE_AT with the address to pick one), or when the delta reaches past the function's
// extent. The extent is the table's `length` when the sweep settled it and its `bound`
// otherwise; the bound is an upper bound, so a delta that lands in untagged code right after
// a function is not caught. The check guards against a grossly wrong delta and a symbol that
// went away after a pull, not against every mistake.
//
// Cost: this header pulls functions.def (26k rows) into a constexpr array. Nothing of it
// reaches the object file as long as the array is only read at compile time, but parsing it is
// paid in every translation unit that includes this header. Include it from the few files that
// define sites, never from a widely included header.

#include <cstddef>
#include <cstdint>

namespace wxl::offsets::site_detail
{
    struct Row
    {
        const char*   name;
        std::uint32_t address;
        std::uint32_t bound;
        std::uint32_t length;
    };

    inline constexpr Row kRows[] = {
#define WXL_OFFSET_FUNCTION(name, address, bound, length, verify, targs, file, line) \
        { name, address, bound, length },
#include "generated/functions.def"
#undef WXL_OFFSET_FUNCTION
    };

    constexpr bool Equal(const char* a, const char* b)
    {
        for (;; ++a, ++b) {
            if (*a != *b) return false;
            if (*a == 0) return true;
        }
    }

    /// The index of '@' in a seam's symbol, or npos when it carries no address.
    constexpr std::size_t AtSign(const char* text)
    {
        for (std::size_t i = 0; text[i]; ++i)
            if (text[i] == '@') return i;
        return static_cast<std::size_t>(-1);
    }

    /// The hexadecimal address written after the '@', as in "SI2::PlaySoundKit@0x004C6A40".
    constexpr std::uint32_t HexAt(const char* text, std::size_t from)
    {
        std::uint32_t value = 0;
        std::size_t   i     = from;
        if (text[i] == '0' && (text[i + 1] == 'x' || text[i + 1] == 'X')) i += 2;
        for (; text[i]; ++i) {
            const char c = text[i];
            const int digit = (c >= '0' && c <= '9')   ? c - '0'
                              : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                              : (c >= 'A' && c <= 'F') ? c - 'A' + 10
                                                       : -1;
            if (digit < 0) return 0;
            value = value * 16u + static_cast<std::uint32_t>(digit);
        }
        return value;
    }

    /// Whether a row's name is exactly the first `n` characters of `spec`.
    constexpr bool EqualUpTo(const char* row, const char* spec, std::size_t n)
    {
        for (std::size_t i = 0; i < n; ++i) {
            if (!row[i] || row[i] != spec[i]) return false;
        }
        return row[n] == 0;
    }

    struct Match
    {
        std::uint32_t address = 0;
        std::uint32_t extent  = 0;   // length when known, else bound; 0 when neither is known
        unsigned      found   = 0;   // distinct addresses carrying the name (or the exact address)
    };

    // Rows at one address can be several (names folded by the linker, one name tagged in two
    // files): they are the same function, so they count once.
    constexpr Match Find(const char* name, std::uint32_t address, bool byAddress)
    {
        Match m;
        std::uint32_t last = 0;
        for (const Row& r : kRows) {
            if (!Equal(r.name, name)) continue;
            if (byAddress && r.address != address) continue;
            if (m.found && r.address == last) continue;
            last = r.address;
            ++m.found;
            m.address = r.address;
            m.extent  = r.length ? r.length : r.bound;
        }
        return m;
    }

    /**
     * @brief The function a seam's symbol names, with an overload told apart by its address.
     *
     * A symbol is either a plain name, which must be carried by exactly one address, or
     * "<name>@<address>" for a name the client gives to more than one function. The second
     * form is the only way a row can reach an overload, and the only reason to write it.
     */
    constexpr Match FindSpec(const char* spec)
    {
        const std::size_t at = AtSign(spec);
        if (at == static_cast<std::size_t>(-1)) return Find(spec, 0, false);

        const std::uint32_t address = HexAt(spec, at + 1);
        Match               m;
        std::uint32_t       last = 0;
        for (const Row& r : kRows) {
            if (!EqualUpTo(r.name, spec, at)) continue;
            if (r.address != address) continue;
            if (m.found && r.address == last) continue;
            last = r.address;
            ++m.found;
            m.address = r.address;
            m.extent  = r.length ? r.length : r.bound;
        }
        return m;
    }
}

// WXL_SITE(name, delta): the address `delta` bytes into the one function called `name`.
// WXL_SITE_AT(name, address, delta): the same for a name carried by several addresses.
// `name` must be a string literal (it is pasted into the diagnostics); the result is a
// constant expression of type uintptr_t.
#define WXL_SITE(name, delta) \
    ([]() consteval -> std::uintptr_t { \
        constexpr ::wxl::offsets::site_detail::Match m = \
            ::wxl::offsets::site_detail::Find(name, 0, false); \
        static_assert(m.found != 0, "WXL_SITE: no function named " name " in functions.def"); \
        static_assert(m.found < 2, "WXL_SITE: " name " has several addresses; use WXL_SITE_AT"); \
        static_assert(m.extent != 0, "WXL_SITE: " name " has no known extent (sentinel row)"); \
        static_assert(static_cast<std::uint64_t>(delta) < m.extent, \
                      "WXL_SITE: delta " #delta " reaches past the end of " name); \
        return static_cast<std::uintptr_t>(m.address) + static_cast<std::uintptr_t>(delta); \
    }())

// WXL_SEAM_SITE(spec, delta): the same for a seam's symbol column, which may be a plain
// name or "<name>@<address>" when the client gives that name to more than one function.
#define WXL_SEAM_SITE(spec, delta) \
    ([]() consteval -> std::uintptr_t { \
        constexpr ::wxl::offsets::site_detail::Match m = \
            ::wxl::offsets::site_detail::FindSpec(spec); \
        static_assert(m.found != 0, "WXL_SEAM_SITE: no function matches " spec); \
        static_assert(m.found < 2, "WXL_SEAM_SITE: " spec " has several addresses; write it as " \
                                   "\"<name>@0x...\" to say which"); \
        static_assert(m.extent != 0, "WXL_SEAM_SITE: " spec " has no known extent"); \
        static_assert(static_cast<std::uint64_t>(delta) < m.extent, \
                      "WXL_SEAM_SITE: delta " #delta " reaches past the end of " spec); \
        return static_cast<std::uintptr_t>(m.address) + static_cast<std::uintptr_t>(delta); \
    }())

#define WXL_SITE_AT(name, addr, delta) \
    ([]() consteval -> std::uintptr_t { \
        constexpr ::wxl::offsets::site_detail::Match m = \
            ::wxl::offsets::site_detail::Find(name, (addr), true); \
        static_assert(m.found != 0, "WXL_SITE_AT: no function named " name " at " #addr); \
        static_assert(m.extent != 0, "WXL_SITE_AT: " name " has no known extent (sentinel row)"); \
        static_assert(static_cast<std::uint64_t>(delta) < m.extent, \
                      "WXL_SITE_AT: delta " #delta " reaches past the end of " name); \
        return static_cast<std::uintptr_t>(m.address) + static_cast<std::uintptr_t>(delta); \
    }())

// --- Statics ------------------------------------------------------------------------------
//
// The same resolution for a static or global out of statics.def: WXL_STATIC(name) is its
// address. A static has no extent, so there is no delta; a name carried by several addresses
// fails the build like a function does.

namespace wxl::offsets::site_detail
{
    struct StaticRow
    {
        const char*   name;
        std::uint32_t address;
    };

    inline constexpr StaticRow kStaticRows[] = {
#define WXL_OFFSET_STATIC(name, address, file, line) { name, address },
#include "generated/statics.def"
#undef WXL_OFFSET_STATIC
    };

    constexpr Match FindStatic(const char* name, std::uint32_t address, bool byAddress)
    {
        Match m;
        std::uint32_t last = 0;
        for (const StaticRow& r : kStaticRows) {
            if (!Equal(r.name, name)) continue;
            if (byAddress && r.address != address) continue;
            if (m.found && r.address == last) continue;
            last = r.address;
            ++m.found;
            m.address = r.address;
        }
        return m;
    }
}

// WXL_STATIC(name): the address of the one static called `name`.
// WXL_STATIC_AT(name, address): the same for a name carried by several addresses, typically a
// file-scope static declared in more than one translation unit.
#define WXL_STATIC(name) \
    ([]() consteval -> std::uintptr_t { \
        constexpr ::wxl::offsets::site_detail::Match m = \
            ::wxl::offsets::site_detail::FindStatic(name, 0, false); \
        static_assert(m.found != 0, "WXL_STATIC: no static named " name " in statics.def"); \
        static_assert(m.found < 2, "WXL_STATIC: " name " has several addresses; use WXL_STATIC_AT"); \
        return static_cast<std::uintptr_t>(m.address); \
    }())

#define WXL_STATIC_AT(name, addr) \
    ([]() consteval -> std::uintptr_t { \
        constexpr ::wxl::offsets::site_detail::Match m = \
            ::wxl::offsets::site_detail::FindStatic(name, (addr), true); \
        static_assert(m.found != 0, "WXL_STATIC_AT: no static named " name " at " #addr); \
        return static_cast<std::uintptr_t>(m.address); \
    }())
