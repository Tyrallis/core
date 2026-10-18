// The wxl.storage service: the client's files, over the binding.
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

#include "services/StorageService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"
#include "wxl/runtime/handles.hpp"

#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr char kTag[] = "storage";

    /// What the client handed back for an open file, and who asked for it.
    struct Open
    {
        void*    file = nullptr;
        uint32_t mod  = 0; ///< the host's token for the mod that opened it
    };

    /// Handles are generational, so one kept past its close answers "gone" rather than reading
    /// whatever the client put at that address next.
    wxl::runtime::HandleTable g_handles;
    std::vector<Open>         g_open;

    /// A boundary string as a NUL-terminated path the client's own calls can take.
    std::string Path(wxl_str text)
    {
        return text.data ? std::string(text.data, text.size) : std::string();
    }

    uint32_t Asking()
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0u;
    }

    void Forget(void* file)
    {
        g_handles.Untrack(file);
        for (std::size_t i = 0; i < g_open.size(); ++i)
        {
            if (g_open[i].file != file) continue;
            g_open.erase(g_open.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
    }

    // --- the table ----------------------------------------------------------------------------

    wxl_bool WXL_CALL Exists(wxl_str path)
    {
        const std::string name = Path(path);
        if (name.empty()) return WXL_FALSE;
        return wxl::core::binding::file_exists(name.c_str());
    }

    /**
     * @brief The length of a file, which costs an open and a close.
     *
     * The client has no way to ask a name how long it is: only an open file answers that. A mod
     * that then reads the file pays for a second open, which is why the facade's whole-file read
     * exists and why this is not the way to read one.
     */
    wxl_result WXL_CALL Size(wxl_str path, uint64_t* out)
    {
        if (!out) return WXL_E_INVALID;
        const std::string name = Path(path);
        if (name.empty()) return WXL_E_INVALID;

        void* file = wxl::core::binding::file_open(name.c_str());
        if (!file) return WXL_E_NOT_FOUND;
        *out = wxl::core::binding::file_size(file);
        wxl::core::binding::file_close(file);
        return WXL_OK;
    }

    wxl_result WXL_CALL Read(wxl_str path, void* buffer, uint64_t capacity, uint64_t* out_read)
    {
        if (!out_read) return WXL_E_INVALID;
        *out_read = 0;

        const std::string name = Path(path);
        if (name.empty()) return WXL_E_INVALID;

        void* file = wxl::core::binding::file_open(name.c_str());
        if (!file) return WXL_E_NOT_FOUND;

        const uint64_t size = wxl::core::binding::file_size(file);
        if (!buffer || capacity < size)
        {
            // What it would have taken, so the caller can size a buffer and come back.
            *out_read = size;
            wxl::core::binding::file_close(file);
            return WXL_E_INVALID;
        }

        uint64_t read = 0;
        while (read < size)
        {
            const uint64_t left = size - read;
            const uint32_t want = left > 0x10000000u ? 0x10000000u : static_cast<uint32_t>(left);
            const uint32_t got  = wxl::core::binding::file_read(
                file, static_cast<char*>(buffer) + read, want);
            if (!got) break;
            read += got;
        }
        wxl::core::binding::file_close(file);

        *out_read = read;
        return read == size ? WXL_OK : WXL_E_FAILED;
    }

    wxl_handle WXL_CALL OpenFile(wxl_str path)
    {
        const std::string name = Path(path);
        if (name.empty()) return WXL_HANDLE_NONE;

        void* file = wxl::core::binding::file_open(name.c_str());
        if (!file) return WXL_HANDLE_NONE;

        g_open.push_back(Open{file, Asking()});
        return g_handles.Track(file);
    }

    wxl_result WXL_CALL ReadAt(wxl_handle handle, uint64_t offset, void* buffer, uint64_t size,
                               uint64_t* out_read)
    {
        if (!out_read) return WXL_E_INVALID;
        *out_read = 0;

        // The handle table answers with the client's file pointer, which is everything a read
        // needs; the mod that opened it only matters when its files are closed for it.
        void* file = g_handles.Resolve(handle);
        if (!file) return WXL_E_NOT_FOUND;
        if (!buffer && size) return WXL_E_INVALID;
        if (!size) return WXL_OK;

        if (!wxl::core::binding::file_seek(file, offset)) return WXL_E_FAILED;

        uint64_t read = 0;
        while (read < size)
        {
            const uint64_t left = size - read;
            const uint32_t want = left > 0x10000000u ? 0x10000000u : static_cast<uint32_t>(left);
            const uint32_t got  = wxl::core::binding::file_read(
                file, static_cast<char*>(buffer) + read, want);
            if (!got) break;
            read += got;
        }

        *out_read = read;
        return WXL_OK;
    }

    void WXL_CALL Close(wxl_handle handle)
    {
        void* file = g_handles.Resolve(handle);
        if (!file) return;
        wxl::core::binding::file_close(file);
        Forget(file);
    }

    wxl_result WXL_CALL MountArchive(wxl_str path, int32_t priority)
    {
        const std::string name = Path(path);
        if (name.empty()) return WXL_E_INVALID;
        if (!wxl::core::binding::archive_mount(name.c_str(), priority)) return WXL_E_FAILED;

        WLOG_INFO(kTag, "archive %s mounted at priority %d", name.c_str(), priority);
        return WXL_OK;
    }

    /**
     * @brief The table, with a hole where this runtime has nothing true to say.
     *
     * add_search_path and find are null rather than filled with something that answers wrongly.
     * The client mounts archives and searches them; it has no notion of a loose directory
     * joining the search path, and no way to list a name across the whole mounted set. A mod
     * asking the SDK for either is told WXL_E_UNSUPPORTED, which is the truth.
     */
    const wxl_storage_api g_storage = {
        sizeof(wxl_storage_api),
        &Exists,
        &Size,
        &Read,
        &OpenFile,
        &ReadAt,
        &Close,
        nullptr, // add_search_path: the client has no such notion
        &MountArchive,
        nullptr, // find: the client cannot list a name across the mounted set
    };
}

namespace wxl::core::services
{
    const wxl_storage_api& StorageApi() { return g_storage; }

    namespace storage
    {
        void CloseEverything()
        {
            if (!g_open.empty())
                WLOG_WARN(kTag, "closing %zu file(s) a mod left open", g_open.size());
            for (const Open& open : g_open) wxl::core::binding::file_close(open.file);
            g_open.clear();
            g_handles.Clear();
        }

        void CloseFilesOf(uint32_t mod)
        {
            for (std::size_t i = g_open.size(); i > 0; --i)
            {
                const Open& open = g_open[i - 1];
                if (open.mod != mod) continue;
                wxl::core::binding::file_close(open.file);
                g_handles.Untrack(open.file);
                g_open.erase(g_open.begin() + static_cast<std::ptrdiff_t>(i - 1));
            }
        }
    }
}
