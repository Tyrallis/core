// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The client binding filled from a wxl_core_init: the x64 runtime's and the harness's realisation.

#include "binding/Binding.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{
    /// The accessors handed over, as far as the caller's wxl_core_init reached.
    wxl_core_init g_init{};

#define WXL_INIT_CARRIES(size, field) \
    ((size) >= offsetof(wxl_core_init, field) + sizeof(wxl_core_init::field))
}

namespace wxl::core::binding
{
    bool Agrees(const wxl_core_init& init, char* why, size_t capacity)
    {
        if (init.struct_size < offsetof(wxl_core_init, rows) + sizeof(init.rows) || !init.rows)
        {
            std::snprintf(why, capacity, "the client hands over no rows: it was built against an "
                                         "older wxl/core/init.h");
            return false;
        }

        const uint32_t mine   = WXL_CORE_INIT_ROW_COUNT;
        const uint32_t shared = init.row_count < mine ? init.row_count : mine;
        for (uint32_t i = 0; i < shared; ++i)
        {
            const char* theirs = init.rows[i] ? init.rows[i] : "(null)";
            if (std::strcmp(theirs, wxl_core_init_rows[i]) == 0) continue;
            std::snprintf(why, capacity, "row %u differs: the client has \"%s\", the runtime \"%s\"",
                          static_cast<unsigned>(i), theirs, wxl_core_init_rows[i]);
            return false;
        }

        // The accessors must be where the rows say: one pointer per row after the header.
        const size_t covered = (init.struct_size - offsetof(wxl_core_init, map_id)) / sizeof(void*);
        if (covered < init.row_count)
        {
            std::snprintf(why, capacity, "the client names %u rows but its struct_size holds %u",
                          static_cast<unsigned>(init.row_count), static_cast<unsigned>(covered));
            return false;
        }
        return true;
    }

    void Bind(const wxl_core_init& init)
    {
        g_init = wxl_core_init{};
#define WXL_BINDING(name, ret, params, args, absent) \
    if (WXL_INIT_CARRIES(init.struct_size, name)) g_init.name = init.name;
#include "wxl/core/binding.def"
#undef WXL_BINDING
        g_init.struct_size = sizeof g_init;
    }

    std::vector<const char*> Unbound()
    {
        std::vector<const char*> names;
#define WXL_BINDING(name, ret, params, args, absent) \
    if (!g_init.name) names.push_back(#name);
#include "wxl/core/binding.def"
#undef WXL_BINDING
        return names;
    }

#define WXL_BINDING(name, ret, params, args, absent) \
    ret name params { return g_init.name ? g_init.name args : (absent); }
#include "wxl/core/binding.def"
#undef WXL_BINDING
}
