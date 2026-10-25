// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Storage seam: what the runtime does at the client's one file-open function.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Stages.hpp"
#include "services/DataService.hpp"
#include "services/StorageService.hpp"

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

    /// The name a folder of the search path answers an open with, held as long as g_redirect.
    thread_local char g_resolved[512];

    /// What the client opens in the end: a folder of the search path that holds the name and
    /// wins it answers with its file, whoever named it. An open from one given archive is that
    /// archive's.
    void Resolve(void** archive, const char** name)
    {
        if (archive && *archive) return;
        if (wxl::core::services::storage::Resolve(*name, g_resolved, sizeof g_resolved))
        {
            WLOG_DEBUG(wxl::core::kTag, "file %s found in a search path folder: %s", *name, g_resolved);
            *name = g_resolved;
        }
    }
}

namespace wxl::seam::Storage::OpenFile
{
    bool WXL_CALL Gate(int32_t* result, void** archive, const char** name, uint32_t* flags,
                       void*** file)
    {
        (void)result;
        (void)file;
        (void)flags;

        // The client is opening a file, so its archives are mounted: whatever else this
        // seam is for, the first call through it is what storage being up looks like.
        wxl::core::stages::Reach(WXL_STAGE_STORAGE);

        if (!name || !*name) return false;

        // The runtime listing the archives for wxl.storage's find: its own open, of an archive's
        // listfile, which nothing else is told of.
        if (wxl::core::services::storage::Listing()) return false;

        // A client database a mod serves is answered here, before anything is raised: the
        // client reads each of its databases once, so this is the only moment that counts.
        // Mods see the open as the path that was asked for, and may still redirect it again.
        const std::size_t length = std::strlen(*name);
        if (wxl::core::services::data::ServedPath(*name, length, g_redirect, sizeof g_redirect))
        {
            *name = g_redirect;
            Resolve(archive, name);
            return false;
        }

        // A mod's redirect, from the table: what the client opens unless a handler answers.
        const char* const asked = *name;
        g_redirect[0]            = 0;
        wxl::core::services::storage::Redirected(asked, g_redirect, sizeof g_redirect);

        wxl::host::ModHost* host = wxl::core::Host();
        if (host && host->Listening(WXL_EV_FileOpen))
        {
            // Handlers see the name asked for, and in `redirect` what the table answers so far.
            wxl_file_open_args args{};
            args.path              = wxl_str{asked, static_cast<uint32_t>(length)};
            args.redirect          = g_redirect;
            args.redirect_capacity = static_cast<uint32_t>(sizeof g_redirect);
            host->Raise(WXL_EV_FileOpen, &args);
        }

        // Nobody answered: the client opens what it asked for, from a folder when one wins it.
        if (!g_redirect[0])
        {
            Resolve(archive, name);
            return false;
        }

        // A handler that writes past the end of the buffer would leave it unterminated; the
        // last byte is forced rather than trusting what came back.
        g_redirect[sizeof g_redirect - 1] = 0;
        WLOG_DEBUG(wxl::core::kTag, "file %s redirected to %s", asked, g_redirect);

        // The one thing only a gate can do: what the client opens is not what it asked for.
        *name = g_redirect;
        Resolve(archive, name);
        return false;
    }
}

namespace wxl::seam::Storage::OpenEx
{
    void WXL_CALL After(int32_t result, void*, const char* name, uint32_t, void** file)
    {
        // Any thread, every open: nothing is read until someone listens.
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !host->Listening(WXL_EV_FileOpened) || !name) return;
        if (wxl::core::services::storage::Listing()) return;

        const bool found = result != 0 && file && *file;
        const uint64_t size = found ? wxl::core::binding::file_size(*file) : 0;

        wxl_file_opened_args args{};
        args.path  = wxl_str{name, static_cast<uint32_t>(std::strlen(name))};
        args.found = found ? WXL_TRUE : WXL_FALSE;
        args.size  = size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
        host->Raise(WXL_EV_FileOpened, &args);
    }
}

namespace
{
    /// An archive's event, as the client describes the archive; false when it describes none.
    bool ArchiveArgs(void* archive, wxl_archive_args* args)
    {
        wxl_core_archive described{};
        if (!archive || !wxl::core::binding::archive_of(archive, &described)) return false;
        const char* path    = described.path ? described.path : "";
        args->path          = wxl_str{path, static_cast<uint32_t>(std::strlen(path))};
        args->priority      = described.priority;
        args->hash_entries  = described.hash_entries;
        args->block_entries = described.block_entries;
        args->bytes         = described.bytes;
        return true;
    }
}

namespace wxl::seam::Storage::OpenArchive
{
    void WXL_CALL After(int32_t result, const char*, int32_t, uint32_t, void** archive)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        wxl_archive_args    args{};
        if (!host || !result || !archive || !ArchiveArgs(*archive, &args)) return;
        host->Raise(WXL_EV_ArchiveMounted, &args);
    }
}

namespace wxl::seam::Storage::CloseArchive
{
    void WXL_CALL Before(void* archive)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        wxl_archive_args    args{};
        if (!host || !ArchiveArgs(archive, &args)) return;
        host->Raise(WXL_EV_ArchiveUnmounted, &args);
    }
}
