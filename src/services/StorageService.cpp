// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.storage service: the client's files, over the binding.

#include "services/StorageService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"
#include "wxl/runtime/handles.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstring>
#include <mutex>
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

    // --- the search path ----------------------------------------------------------------------
    //
    // The client resolves a name in its archives, and among loose files only when asked to: the
    // open's flag 1 makes its FindFile look the name up in a table of the files under its folder,
    // which it builds on first use (SFile::RebuildHash starts it over). So a folder joins the
    // search here, in the runtime: at each open, a folder that holds the name and wins it turns the
    // name into the file's path relative to the client's folder, with that flag. A folder must be
    // inside the client's folder; a mod's, in Mods\, is.

    struct SearchFolder
    {
        std::string directory; ///< absolute, backslashes, no trailing separator
        int32_t     priority;
        bool        outside_warned = false;
    };

    std::mutex              g_pathsLock;
    std::vector<SearchFolder> g_paths; ///< highest priority first; equal ones in the order added
    std::atomic<uint32_t>   g_pathCount{0};
    std::atomic<bool>       g_rescan{false};
    std::atomic<uint32_t>   g_generation{0}; ///< changes when what find answers may have
    thread_local bool       t_listing = false;

    std::string Backslashed(std::string path)
    {
        std::replace(path.begin(), path.end(), '/', '\\');
        return path;
    }

    bool IsFile(const std::string& path)
    {
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    bool IsDirectory(const std::string& path)
    {
        const DWORD attributes = ::GetFileAttributesA(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    /// A name the client resolves itself: absolute, a share, or climbing out.
    bool IsPlainName(const char* name)
    {
        if (!name || !*name) return false;
        if (name[0] == '\\' || name[0] == '/' || name[1] == ':') return false;
        return std::strstr(name, "..") == nullptr;
    }

    /// The client's folder, with its separator: its base path, or the current directory while it
    /// has none, which is what its own scan falls back to.
    std::string ClientFolder()
    {
        const char* base = wxl::core::binding::storage_base_path();
        std::string folder;
        if (base && *base)
        {
            folder = Backslashed(base);
        }
        else
        {
            char        current[MAX_PATH];
            const DWORD length = ::GetCurrentDirectoryA(MAX_PATH, current);
            if (length && length < MAX_PATH) folder.assign(current, length);
        }
        if (!folder.empty() && folder.back() != '\\') folder += '\\';
        return folder;
    }

    /// Whether a folder is inside the client's folder, where the client can open its files.
    bool Inside(const std::string& directory, const std::string& folder)
    {
        return !folder.empty() && directory.size() + 1 >= folder.size() &&
               _strnicmp((directory + "\\").c_str(), folder.c_str(), folder.size()) == 0;
    }

    // --- the table ----------------------------------------------------------------------------

    wxl_bool WXL_CALL Exists(wxl_str path)
    {
        const std::string name = Path(path);
        if (name.empty()) return WXL_FALSE;
        return wxl::core::services::storage::Exists(name.c_str()) ? WXL_TRUE : WXL_FALSE;
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

        g_generation.fetch_add(1, std::memory_order_relaxed);
        WLOG_INFO(kTag, "archive %s mounted at priority %d", name.c_str(), priority);
        return WXL_OK;
    }

    /**
     * @brief A folder joins the search: absolute, or relative to the current directory, which
     * is the client's folder once the client has set its paths.
     *
     * Adding a folder again changes its priority. The client is told to scan its loose files
     * again at its next open, so a file put in a folder since is found too.
     */
    wxl_result WXL_CALL AddSearchPath(wxl_str directory, int32_t priority)
    {
        std::string asked = Backslashed(Path(directory));
        while (asked.size() > 1 && asked.back() == '\\') asked.pop_back();
        if (asked.empty()) return WXL_E_INVALID;

        char        full[MAX_PATH * 2];
        const DWORD length = ::GetFullPathNameA(asked.c_str(), sizeof full, full, nullptr);
        if (!length || length >= sizeof full) return WXL_E_INVALID;
        std::string absolute(full, length);
        while (absolute.size() > 3 && absolute.back() == '\\') absolute.pop_back();
        if (!IsDirectory(absolute))
        {
            WLOG_WARN(kTag, "search path %s: no such folder", absolute.c_str());
            return WXL_E_NOT_FOUND;
        }

        {
            std::lock_guard<std::mutex> lock(g_pathsLock);
            const auto same = std::find_if(g_paths.begin(), g_paths.end(), [&](const SearchFolder& known) {
                return _stricmp(known.directory.c_str(), absolute.c_str()) == 0;
            });
            if (same != g_paths.end()) g_paths.erase(same);
            const auto at = std::find_if(g_paths.begin(), g_paths.end(),
                                         [&](const SearchFolder& known) { return known.priority < priority; });
            g_paths.insert(at, SearchFolder{absolute, priority});
            g_pathCount.store(static_cast<uint32_t>(g_paths.size()), std::memory_order_release);
        }
        g_rescan.store(true, std::memory_order_release);
        g_generation.fetch_add(1, std::memory_order_relaxed);
        WLOG_INFO(kTag, "folder %s searched at priority %d", absolute.c_str(), priority);
        return WXL_OK;
    }

    // --- find ---------------------------------------------------------------------------------

    char Lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

    /// Whether a name matches a pattern, `*` any run of characters, case and separators aside.
    bool Matches(const char* name, const char* pattern)
    {
        const char* star  = nullptr;
        const char* after = nullptr;
        while (*name)
        {
            const char p = *pattern == '/' ? '\\' : *pattern;
            const char n = *name == '/' ? '\\' : *name;
            if (p == '*')
            {
                star  = pattern++;
                after = name;
            }
            else if (p && Lower(p) == Lower(n))
            {
                ++pattern;
                ++name;
            }
            else if (star)
            {
                pattern = star + 1;
                name    = ++after;
            }
            else
            {
                return false;
            }
        }
        while (*pattern == '*') ++pattern;
        return *pattern == 0;
    }

    struct Found
    {
        const char*              pattern;
        std::vector<std::string> names;
        std::vector<std::string> keys; ///< lowercased, to report each name once
    };

    void Keep(Found& found, const std::string& name)
    {
        if (!Matches(name.c_str(), found.pattern)) return;
        std::string key = Backslashed(name);
        std::transform(key.begin(), key.end(), key.begin(), Lower);
        found.keys.push_back(std::move(key));
        found.names.push_back(Backslashed(name));
    }

    int32_t WXL_CALL KeepListed(const char* name, void* user)
    {
        if (name && *name) Keep(*static_cast<Found*>(user), name);
        return 1;
    }

    void KeepFolder(Found& found, const std::string& root, const std::string& prefix)
    {
        WIN32_FIND_DATAA entry;
        const HANDLE     search = ::FindFirstFileA((root + "\\" + prefix + "*").c_str(), &entry);
        if (search == INVALID_HANDLE_VALUE) return;
        do
        {
            const std::string leaf = entry.cFileName;
            if (leaf == "." || leaf == "..") continue;
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                KeepFolder(found, root, prefix + leaf + "\\");
            else
                Keep(found, prefix + leaf);
        } while (::FindNextFileA(search, &entry));
        ::FindClose(search);
    }

    /**
     * @brief The names matching a pattern, across the archives' listfiles and the folders.
     *
     * Listing every archive reads every listfile, so the answer is kept for the thread's next
     * call with the same pattern -- the count, then the fill -- until an archive or a folder
     * joins.
     */
    uint32_t WXL_CALL Find(wxl_str pattern, wxl_str* out, uint32_t capacity)
    {
        thread_local std::string              lastPattern;
        thread_local uint32_t                 lastGeneration = ~0u;
        thread_local std::vector<std::string> answer;

        const std::string wanted     = Backslashed(Path(pattern));
        const uint32_t    generation = g_generation.load(std::memory_order_relaxed);
        if (wanted.empty()) return 0;
        if (wanted != lastPattern || generation != lastGeneration)
        {
            Found found{wanted.c_str(), {}, {}};
            t_listing = true;
            wxl::core::binding::archive_list(&KeepListed, &found);
            t_listing = false;

            std::vector<std::string> folders;
            {
                const std::string           client = ClientFolder();
                std::lock_guard<std::mutex> lock(g_pathsLock);
                for (const SearchFolder& path : g_paths)
                    if (Inside(path.directory, client)) folders.push_back(path.directory);
            }
            for (const std::string& folder : folders) KeepFolder(found, folder, std::string());

            // Once each: the first source to name it, sorted by name.
            std::vector<size_t> order(found.names.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = i;
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) { return found.keys[a] < found.keys[b]; });
            answer.clear();
            for (size_t i = 0; i < order.size(); ++i)
                if (i == 0 || found.keys[order[i]] != found.keys[order[i - 1]])
                    answer.push_back(found.names[order[i]]);
            lastPattern    = wanted;
            lastGeneration = generation;
        }

        for (uint32_t i = 0; out && i < capacity && i < answer.size(); ++i)
            out[i] = wxl_str{answer[i].c_str(), static_cast<uint32_t>(answer[i].size())};
        return static_cast<uint32_t>(answer.size());
    }

    const wxl_storage_api g_storage = {
        sizeof(wxl_storage_api),
        &Exists,
        &Size,
        &Read,
        &OpenFile,
        &ReadAt,
        &Close,
        &AddSearchPath,
        &MountArchive,
        &Find,
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

        bool Listing() { return t_listing; }

        void ForgetSearchPaths()
        {
            std::lock_guard<std::mutex> lock(g_pathsLock);
            g_paths.clear();
            g_pathCount.store(0, std::memory_order_release);
            g_generation.fetch_add(1, std::memory_order_relaxed);
        }

        bool Exists(const char* name)
        {
            if (!name || !*name) return false;
            if (wxl::core::binding::file_exists(name)) return true;
            if (!IsPlainName(name) || g_pathCount.load(std::memory_order_acquire) == 0) return false;
            const std::string           relative = Backslashed(name);
            const std::string           client   = ClientFolder();
            std::lock_guard<std::mutex> lock(g_pathsLock);
            for (const SearchFolder& path : g_paths)
                if (Inside(path.directory, client) && IsFile(path.directory + "\\" + relative)) return true;
            return false;
        }

        bool Resolve(const char* name, char* out, size_t capacity, uint32_t* flags)
        {
            if (!out || !capacity || !flags) return false;
            if (g_pathCount.load(std::memory_order_acquire) == 0 || !IsPlainName(name)) return false;

            const std::string relative = Backslashed(name);
            std::string       file;
            {
                std::lock_guard<std::mutex> lock(g_pathsLock);
                for (SearchFolder& path : g_paths)
                {
                    const std::string candidate = path.directory + "\\" + relative;
                    if (!IsFile(candidate)) continue;
                    // A folder at 0 or below only fills what the client does not have.
                    if (path.priority <= 0 && wxl::core::binding::file_exists(name)) return false;
                    const std::string folder = ClientFolder();
                    if (!Inside(path.directory, folder))
                    {
                        if (!path.outside_warned)
                            WLOG_WARN(kTag, "folder %s is outside the client's folder %s; the client "
                                            "cannot open its files",
                                      path.directory.c_str(), folder.c_str());
                        path.outside_warned = true;
                        continue;
                    }
                    file = candidate.substr(folder.size());
                    break;
                }
            }
            if (file.empty() || file.size() + 1 > capacity) return false;

            // The client scans its loose files once; a folder added since needs a new scan.
            if (g_rescan.exchange(false, std::memory_order_acq_rel)) wxl::core::binding::storage_rescan();
            std::memcpy(out, file.c_str(), file.size() + 1);
            *flags |= 1u;
            return true;
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
