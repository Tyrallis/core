// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// wxl.glue-unlock: bypasses the client's Lua/XML signature check so custom interface code loads.
//
// Byte set ported from a third-party extension pack (MIT, (c) Alyst3r).

#include <cstdio>

#include "wxl/sdk.hpp"
#include "wxl/services/patch.h"

#include "wxl/offsets/site.hpp"

namespace
{
    constexpr const char* kTag = "glue-unlock";

    /// One site: the bytes the stock client has there, and the bytes that bypass the check.
    struct Edit
    {
        const char* name;
        uintptr_t   address;
        uint8_t     stock[8];
        uint8_t     patched[8];
        uint32_t    size;
    };

    // The signature check runs in FrameXML_CheckSignature; DeleteInterfaceFiles is where the
    // client discards an interface it judged unsigned. Each edit either turns a conditional
    // branch into an unconditional one (jz/jnz/jg -> jmp) or replaces the status the check
    // returns with the "signed" value, 3.
    const Edit kEdits[] = {
        { "DeleteInterfaceFiles+2F",     WXL_SITE("DeleteInterfaceFiles", 0x2F),        { 0x74 }, { 0xEB }, 1 },
        { "FrameXML_CheckSignature+45",  WXL_SITE("FrameXML_CheckSignature", 0x45),     { 0x75 }, { 0xEB }, 1 },
        { "FrameXML_CheckSignature+5F",  WXL_SITE("FrameXML_CheckSignature", 0x5F),     { 0x01 }, { 0x03 }, 1 },
        { "FrameXML_CheckSignature+B5",  WXL_SITE("FrameXML_CheckSignature", 0xB5),     { 0x01 }, { 0x03 }, 1 },
        { "FrameXML_CheckSignature+166", WXL_SITE("FrameXML_CheckSignature", 0x166),    { 0x7F }, { 0xEB }, 1 },
        { "FrameXML_CheckSignature+17F", WXL_SITE("FrameXML_CheckSignature", 0x17F),
          { 0x83, 0xC0, 0x03, 0x5E, 0x8B, 0xE5, 0x5D }, { 0xB8, 0x03, 0x00, 0x00, 0x00, 0xEB, 0xED }, 7 },
    };

    wxl::Service<wxl_patch_api> g_patch(WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION);

    class GlueUnlockMod final : public wxl::Mod
    {
    public:
        wxl_result OnLoad() override
        {
            if (!g_patch)
            {
                wxl::Log(WXL_LOG_ERROR, kTag, "wxl.patch is not published; nothing applied");
                return WXL_E_UNSUPPORTED;
            }

            wxl_patch applied[sizeof kEdits / sizeof kEdits[0]];
            unsigned  count = 0, present = 0;
            char      line[160];
            for (const Edit& edit : kEdits)
            {
                // A client whose executable already carries the edit (a static patch) is left
                // as it is: the check is bypassed either way.
                if (g_patch->matches(edit.address, edit.patched, edit.size))
                {
                    ++present;
                    continue;
                }
                const wxl_patch patch = g_patch->apply(edit.name, edit.address, edit.stock, edit.patched, edit.size);
                if (patch == WXL_PATCH_NONE)
                {
                    std::snprintf(line, sizeof line, "%s @0x%08X refused; undoing %u edit(s) and standing down", edit.name, static_cast<unsigned>(edit.address), count);
                    wxl::Log(WXL_LOG_ERROR, kTag, line);
                    while (count) g_patch->restore(applied[--count]);
                    return WXL_E_FAILED;
                }
                applied[count++] = patch;
            }

            std::snprintf(line, sizeof line, "signature check bypassed: %u edit(s) applied, %u already present", count, present);
            wxl::Log(WXL_LOG_INFO, kTag, line);
            return WXL_OK;
        }
    };
} // namespace

WXL_MOD(GlueUnlockMod, "wxl.glue-unlock", "Glue unlock", 1)
