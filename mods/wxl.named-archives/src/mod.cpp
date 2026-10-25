// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.named-archives: widens the client's patch-archive directory scan from a single-letter
// wildcard to an arbitrary one, so an archive or folder can be named "Patch-Interface.MPQ"
// instead of only "Patch-A.MPQ".."Patch-Z.MPQ".
//
// The client hands "patch-?.MPQ" and "patch-%s-?.MPQ" (the locale variant) to its directory
// listing as FindFirstFile wildcards, not printf specifiers: '?' matches exactly one character,
// so the stock scan only ever finds single-letter names. '*' matches any run of characters, so
// the same scan then also finds "Patch-<anything>.MPQ", an archive or a plain directory of that
// name, which the engine already knows how to mount. The two literals are found by content in
// the image's read-only data: the offset tables carry no symbol for them.

#include <cstdio>
#include <cstring>

#include "wxl/sdk.hpp"
#include "wxl/services/patch.h"

namespace
{
    constexpr const char* kTag = "named-archives";

    /// One literal: the stock text, the same with '?' widened, and where the '?' sits.
    struct Literal
    {
        const char* name;
        const char* stock;
        const char* widened;
        uint32_t    wildcard;
    };

    // The terminator is part of the pattern so a longer literal with the same prefix is not
    // taken for the one wanted.
    const Literal kLiterals[] = {
        { "patch-%s-?.MPQ", "patch-%s-?.MPQ", "patch-%s-*.MPQ", 9 },
        { "patch-?.MPQ",    "patch-?.MPQ",    "patch-*.MPQ",    6 },
    };

    wxl::Service<wxl_patch_api> g_patch(WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION);

    class NamedArchivesMod final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            if (!g_patch)
            {
                wxl::Log(WXL_LOG_ERROR, kTag, "wxl.patch is not published; nothing applied");
                return WXL_E_UNSUPPORTED;
            }

            wxl_patch applied[sizeof kLiterals / sizeof kLiterals[0]];
            unsigned  count = 0, present = 0;
            char      line[160];
            for (const Literal& literal : kLiterals)
            {
                const uint32_t size = static_cast<uint32_t>(std::strlen(literal.stock)) + 1;

                // A client whose executable already carries the widened literal (a static
                // patch) is left as it is.
                if (g_patch->find(literal.widened, size, 0) != 0)
                {
                    ++present;
                    continue;
                }

                const uintptr_t at = g_patch->find(literal.stock, size, 0);
                if (at == 0)
                {
                    std::snprintf(line, sizeof line, "\"%s\" not found in the image; undoing %u edit(s)", literal.name, count);
                    wxl::Log(WXL_LOG_ERROR, kTag, line);
                    while (count) g_patch->restore(applied[--count]);
                    return WXL_E_FAILED;
                }

                if (g_patch->find(literal.stock, size, at) != 0)
                {
                    std::snprintf(line, sizeof line, "\"%s\" found more than once; undoing %u edit(s)", literal.name, count);
                    wxl::Log(WXL_LOG_ERROR, kTag, line);
                    while (count) g_patch->restore(applied[--count]);
                    return WXL_E_FAILED;
                }

                const uint8_t   question = '?', star = '*';
                const wxl_patch patch = g_patch->apply(literal.name, at + literal.wildcard, &question, &star, 1);
                if (patch == WXL_PATCH_NONE)
                {
                    std::snprintf(line, sizeof line, "\"%s\" @0x%08X refused; undoing %u edit(s)", literal.name, static_cast<unsigned>(at), count);
                    wxl::Log(WXL_LOG_ERROR, kTag, line);
                    while (count) g_patch->restore(applied[--count]);
                    return WXL_E_FAILED;
                }
                applied[count++] = patch;
            }

            std::snprintf(line, sizeof line, "patch archive scan widened: %u literal(s) edited, %u already present", count, present);
            wxl::Log(WXL_LOG_INFO, kTag, line);
            return WXL_OK;
        }
    };
} // namespace

WXL_MOD(NamedArchivesMod, "wxl.named-archives", "Named archives")
