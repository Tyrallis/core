// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The ClientDb seam: the client databases, once the client has loaded them.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    /// Raises ClientDbLoaded for one database the client left loaded.
    void WXL_CALL Announce(const wxl_core_db* db, void* user)
    {
        if (!db || !db->loaded || !db->file) return;
        wxl_client_db_args args{};
        args.name         = wxl_str{db->file, static_cast<uint32_t>(std::strlen(db->file))};
        args.records      = db->blocks[0];
        args.record_count = db->records;
        args.record_size  = db->record_size;
        static_cast<wxl::host::ModHost*>(user)->Raise(WXL_EV_ClientDbLoaded, &args);
    }
}

namespace wxl::seam::ClientDb::Initialize
{
    void WXL_CALL After()
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        wxl::core::binding::client_dbs(&Announce, host);
    }
}
