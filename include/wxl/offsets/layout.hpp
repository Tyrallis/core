// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// Compile-time resolution of a measured struct layout: type id + member, checked.

#pragma once

// A field offset or a struct size is written as the layouts.def id plus the member designator,
// and resolved while compiling:
//
//     // The unit descriptor block a CGUnit_C points at.
//     constexpr size_t kUnitDesc = WXL_FIELD("CGUnit_C", "m_unit");
//
// The build fails, with a message naming the pair, when the id or the member is not in
// layouts.def: a member that moved or went away after a regeneration becomes a compile error
// rather than a read at the wrong offset. The member is spelled as layouts.txt declares it
// ("m_movementData.m_position", "m_texTarget[0]").
//
// Like site.hpp, this header parses the whole table in every translation unit that includes
// it. Include it from the few files that define offsets, never from a widely included header.

#include <cstddef>
#include <cstdint>

namespace wxl::offsets::layout_detail
{
    struct FieldRow
    {
        const char*   type;
        const char*   member;
        std::uint32_t offset;
    };

    struct TypeRow
    {
        const char*   type;
        std::uint32_t size;
    };

    inline constexpr FieldRow kFields[] = {
#define WXL_LAYOUT_TYPE(id, type, size, file, line)
#define WXL_LAYOUT_FIELD(id, member, offset, file, line) { #id, member, offset },
#define WXL_LAYOUT_SIZE(id, member, size, file, line)
#include "generated/layouts.def"
#undef WXL_LAYOUT_TYPE
#undef WXL_LAYOUT_FIELD
#undef WXL_LAYOUT_SIZE
    };

    inline constexpr TypeRow kTypes[] = {
#define WXL_LAYOUT_TYPE(id, type, size, file, line) { #id, size },
#define WXL_LAYOUT_FIELD(id, member, offset, file, line)
#define WXL_LAYOUT_SIZE(id, member, size, file, line)
#include "generated/layouts.def"
#undef WXL_LAYOUT_TYPE
#undef WXL_LAYOUT_FIELD
#undef WXL_LAYOUT_SIZE
    };

    inline constexpr FieldRow kSizes[] = {
#define WXL_LAYOUT_TYPE(id, type, size, file, line)
#define WXL_LAYOUT_FIELD(id, member, offset, file, line)
#define WXL_LAYOUT_SIZE(id, member, size, file, line) { #id, member, size },
#include "generated/layouts.def"
#undef WXL_LAYOUT_TYPE
#undef WXL_LAYOUT_FIELD
#undef WXL_LAYOUT_SIZE
    };

    constexpr bool Equal(const char* a, const char* b)
    {
        for (;; ++a, ++b) {
            if (*a != *b) return false;
            if (*a == 0) return true;
        }
    }

    struct Match
    {
        std::uint32_t value = 0;
        unsigned      found = 0;
    };

    constexpr Match FindField(const char* type, const char* member)
    {
        Match m;
        for (const FieldRow& r : kFields) {
            if (!Equal(r.type, type) || !Equal(r.member, member)) continue;
            ++m.found;
            m.value = r.offset;
        }
        return m;
    }

    constexpr Match FindSize(const char* type, const char* member)
    {
        Match m;
        for (const FieldRow& r : kSizes) {
            if (!Equal(r.type, type) || !Equal(r.member, member)) continue;
            ++m.found;
            m.value = r.offset;
        }
        return m;
    }

    constexpr Match FindType(const char* type)
    {
        Match m;
        for (const TypeRow& r : kTypes) {
            if (!Equal(r.type, type)) continue;
            ++m.found;
            m.value = r.size;
        }
        return m;
    }
}

// WXL_FIELD(type, member): the byte offset of `member` in the type carrying id `type`.
// WXL_TYPE_SIZE(type): sizeof that type. Both arguments are string literals (they are pasted
// into the diagnostics); the result is a constant expression of type size_t.
#define WXL_FIELD(type, member) \
    ([]() consteval -> std::size_t { \
        constexpr ::wxl::offsets::layout_detail::Match m = \
            ::wxl::offsets::layout_detail::FindField(type, member); \
        static_assert(m.found != 0, "WXL_FIELD: no member " member " of " type " in layouts.def"); \
        static_assert(m.found < 2, "WXL_FIELD: " type "::" member " is measured twice"); \
        return static_cast<std::size_t>(m.value); \
    }())

#define WXL_MEMBER_SIZE(type, member) \
    ([]() consteval -> std::size_t { \
        constexpr ::wxl::offsets::layout_detail::Match m = \
            ::wxl::offsets::layout_detail::FindSize(type, member); \
        static_assert(m.found != 0, "WXL_MEMBER_SIZE: no sizeof(" member ") of " type " in layouts.def"); \
        static_assert(m.found < 2, "WXL_MEMBER_SIZE: sizeof(" type "::" member ") is measured twice"); \
        return static_cast<std::size_t>(m.value); \
    }())

#define WXL_TYPE_SIZE(type) \
    ([]() consteval -> std::size_t { \
        constexpr ::wxl::offsets::layout_detail::Match m = \
            ::wxl::offsets::layout_detail::FindType(type); \
        static_assert(m.found != 0, "WXL_TYPE_SIZE: no type " type " in layouts.def"); \
        static_assert(m.found < 2, "WXL_TYPE_SIZE: " type " is measured twice"); \
        return static_cast<std::size_t>(m.value); \
    }())
