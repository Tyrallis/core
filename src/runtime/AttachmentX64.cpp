// The x64 attachment: calls the client's own source makes. No detour, no address.
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

#include "runtime/Attachment.hpp"
#include "runtime/AttachmentX64.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "seams/Table.hpp"

#include "wxl/services/hook.h"

#include <cstddef>
#include <cstring>

namespace
{
    /// The client's accessors, as far as its wxl_core_init reached.
    wxl_core_init g_init{};

/// Whether a wxl_core_init of the given struct_size carries @p field.
#define WXL_INIT_CARRIES(size, field) \
    ((size) >= offsetof(wxl_core_init, field) + sizeof(wxl_core_init::field))
}

namespace wxl::core::attachment
{
    void Bind(const wxl_core_init& init)
    {
        g_init = wxl_core_init{};
        if (WXL_INIT_CARRIES(init.struct_size, map_id)) g_init.map_id = init.map_id;
        g_init.struct_size = sizeof g_init;
    }

    void Prepare() {}

    std::vector<wxl_event> Events()
    {
        // The seam bodies are the same code on both architectures, so the events they raise are
        // the same list: the one the seam table derives.
        return seams::RaisedEvents();
    }

    std::vector<wxl::host::PublishedService> Services()
    {
        return {};
    }

    void Install()
    {
        // Nothing to attach: the client's source calls Before and After itself. What is checked
        // is what it handed over, since a seam's body reads the client through it.
        for (const seams::Seam& seam : seams::Table())
            WLOG_INFO(kTag, "seam %s: Before and After are calls %s makes", seam.name, seam.symbol);
        if (!g_init.map_id)
            WLOG_WARN(kTag, "the client handed no map_id accessor; world events will carry map -1");
    }

    int32_t MapId()
    {
        return g_init.map_id ? g_init.map_id() : -1;
    }
}
