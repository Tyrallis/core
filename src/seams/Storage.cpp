// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Storage seam: what the runtime does at the client's one file-open function.

#include "wxl/seams/seams.h"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Stages.hpp"
#include "services/DataService.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    /**
     * @brief Where a redirected path lives between the gate and the open.
     *
     * The gate returns before the client opens anything, so the path a handler writes cannot
     * sit on the gate's stack: the client would read freed memory. It cannot be one buffer for
     * the process either, because files are opened from more than one thread. One per thread,
     * overwritten by that thread's next open, which is as long as the client needs it.
     */
    thread_local char g_redirect[512];
}

namespace wxl::seam::Storage::OpenFile
{
    bool WXL_CALL Gate(int32_t* result, void** archive, const char** name, uint32_t* flags,
                       void*** file)
    {
        (void)result;
        (void)archive;
        (void)flags;
        (void)file;

        // The client is opening a file, so its archives are mounted: whatever else this
        // seam is for, the first call through it is what storage being up looks like.
        wxl::core::stages::Reach(WXL_STAGE_STORAGE);

        if (!name || !*name) return false;

        // A client database a mod serves is answered here, before anything is raised: the
        // client reads each of its databases once, so this is the only moment that counts.
        // Mods see the open as the path that was asked for, and may still redirect it again.
        const std::size_t length = std::strlen(*name);
        if (wxl::core::services::data::ServedPath(*name, length, g_redirect, sizeof g_redirect))
        {
            *name = g_redirect;
            return false;
        }

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return false;

        g_redirect[0] = 0;

        wxl_file_open_args args{};
        args.path              = wxl_str{*name, static_cast<uint32_t>(length)};
        args.redirect          = g_redirect;
        args.redirect_capacity = static_cast<uint32_t>(sizeof g_redirect);

        host->Raise(WXL_EV_FileOpen, &args);

        // Nobody answered: the client opens what it asked for, and the gate costs it one call.
        if (!g_redirect[0]) return false;

        // A handler that writes past the end of the buffer would leave it unterminated; the
        // last byte is forced rather than trusting what came back.
        g_redirect[sizeof g_redirect - 1] = 0;
        WLOG_DEBUG(wxl::core::kTag, "file %s redirected to %s", *name, g_redirect);

        // The one thing only a gate can do: what the client opens is not what it asked for.
        *name = g_redirect;
        return false;
    }
}
