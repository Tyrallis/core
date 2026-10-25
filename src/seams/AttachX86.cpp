// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The x86 realisation of the seam table: the detours generated from seams.def.

#include "seams/AttachX86.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "runtime/Core.hpp"
#include "seams/Detour.hpp"
#include "seams/RegparmX86.hpp"
#include "seams/Table.hpp"

#include "wxl/offsets/site.hpp"
#include "wxl/seams/seams.h"

#include <cstddef>
#include <cstdint>

namespace
{
    // One Link per row: the detour in the row's convention, with the row's own entry
    // points. A gated row's Link runs the original only if its Gate lets it through.
#define WXL_SEAM_LINK(domain, name, before, after)                                           \
    using domain##_##name =                                                                  \
        ::wxl::core::seams::DetourOf<::wxl::seam::domain::name::Convention,                  \
                                     ::wxl::seam::domain::name::Signature>::Link<before, after>;
#define WXL_SEAM(domain, name, symbol, ret, conv, params)                                    \
    WXL_SEAM_LINK(domain, name, &::wxl::seam::domain::name::Before,                          \
                  &::wxl::seam::domain::name::After)
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params)                             \
    WXL_SEAM_LINK(domain, name, &::wxl::seam::domain::name::Before, nullptr)
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params)                              \
    WXL_SEAM_LINK(domain, name, nullptr, &::wxl::seam::domain::name::After)
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params)                               \
    using domain##_##name =                                                                  \
        ::wxl::core::seams::GateOf<::wxl::seam::domain::name::Convention,                    \
                                   ::wxl::seam::domain::name::Signature>::Link<              \
            &::wxl::seam::domain::name::Gate>;
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
#undef WXL_SEAM_LINK

    struct Row
    {
        const char* name;    ///< the point's name, "<domain>.<name>", as the log and wxl.hook say it
        uintptr_t   address; ///< the function's address in Wow.exe build 12340
        void*       detour;
        void**      original;
        bool        attachable; ///< false for a regparm row, which is attached through its thunk
        bool        gated;      ///< a WXL_SEAM_GATE row: its Gate can take the call
    };

    const Row kRows[] = {
#define WXL_SEAM_ROW(domain, name, symbol, gated)                                                \
    { #domain "." #name, WXL_SEAM_SITE(symbol, 0), reinterpret_cast<void*>(&domain##_##name::Detour), \
      reinterpret_cast<void**>(&domain##_##name::original), domain##_##name::kAttachable,        \
      gated },
#define WXL_SEAM(domain, name, symbol, ret, conv, params) WXL_SEAM_ROW(domain, name, symbol, false)
#define WXL_SEAM_BEFORE WXL_SEAM
#define WXL_SEAM_AFTER WXL_SEAM
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params) WXL_SEAM_ROW(domain, name, symbol, true)
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
#undef WXL_SEAM_ROW
    };
}

namespace wxl::core::seams
{
    bool Install()
    {
        bool all = true;
        for (const Row& row : kRows)
        {
            // A seam whose body needs what only the recompiled client has is not attached here.
            if (const char* why = seams::X64Only(row.name))
            {
                WLOG_INFO(kTag, "seam %s not attached: x64 only: %s", row.name, why);
                continue;
            }

            // A regparm row is served by the thunk written for its registers.
            const seams::regparm::Row* thunk =
                row.attachable ? nullptr : seams::regparm::Find(row.name);
            if (!row.attachable && !thunk)
            {
                WLOG_ERROR(kTag, "seam %s not attached at 0x%08zX: it takes its arguments in "
                                 "registers and src/seams/RegparmX86.cpp has no thunk for it",
                           row.name, static_cast<size_t>(row.address));
                all = false;
                continue;
            }

            const wxl::hook::Id id =
                wxl::hook::Attach(row.name, reinterpret_cast<void*>(row.address),
                                  thunk ? thunk->detour : row.detour,
                                  thunk ? thunk->original : row.original);
            if (id == wxl::hook::kNone)
            {
                WLOG_ERROR(kTag, "seam %s not attached at 0x%08zX", row.name,
                           static_cast<size_t>(row.address));
                all = false;
                continue;
            }
            WLOG_INFO(kTag, "seam %s attached at 0x%08zX%s%s", row.name,
                      static_cast<size_t>(row.address), row.gated ? ", gated" : "",
                      thunk ? ", through its register thunk" : "");
        }
        return all;
    }
}
