// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.data service: which client databases a mod serves from a file of its own.

#include "services/DataService.hpp"
#include "services/StorageService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"

#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr char kTag[] = "data";

    /// One table a mod serves, and the mod that claimed it.
    struct Served
    {
        std::string table; ///< the basename, lowercased: "spell.dbc"
        std::string path;  ///< what the client opens instead
        uint32_t    mod = 0;
        bool        used = false; ///< the client has read it; an override now is too late
    };

    // SRWLOCK rather than std::mutex, for the reason the log gives: zero-initialised, with no
    // constructor and no destructor, so it holds from the first call to the last. It is needed
    // because the client opens files from more than one thread, while mods claim and give back
    // tables from their own, and both reach the two sets below.
    SRWLOCK g_lock = SRWLOCK_INIT;

    std::vector<Served>      g_served;
    std::vector<std::string> g_loaded; ///< every table the client has read, basename lowercased

    /// Held across a whole operation. Every one of them is cold: the hot path never gets here.
    struct Held
    {
        Held() { AcquireSRWLockExclusive(&g_lock); }
        ~Held() { ReleaseSRWLockExclusive(&g_lock); }
        Held(const Held&)            = delete;
        Held& operator=(const Held&) = delete;
    };

    char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    /// Longer than any name the client's databases carry: one that does not fit is not one.
    constexpr std::size_t kKeyMax = 128;

    /**
     * @brief A table's name reduced to what identifies it: the file, lowercased.
     *
     * The client names a table by its full path, "DBFilesClient\Spell.dbc", and a mod is
     * likely to write just "Spell.dbc". Database names are unique across the client, so the
     * folder carries no information and dropping it makes both spellings the same table.
     *
     * Written into the caller's buffer and terminated there, so no key is ever allocated.
     */
    std::string_view KeyInto(const char* text, std::size_t size, char (&out)[kKeyMax])
    {
        out[0] = 0;
        if (!text) return {};

        std::size_t start = 0;
        for (std::size_t i = 0; i < size; ++i)
            if (text[i] == '\\' || text[i] == '/') start = i + 1;

        const std::size_t length = size - start;
        if (length >= kKeyMax) return {};
        for (std::size_t i = 0; i < length; ++i) out[i] = Lower(text[start + i]);
        out[length] = 0;
        return std::string_view(out, length);
    }

    Served* Find(std::string_view key)
    {
        for (Served& served : g_served)
            if (served.table == key) return &served;
        return nullptr;
    }

    bool Loaded(std::string_view key)
    {
        for (const std::string& table : g_loaded)
            if (table == key) return true;
        return false;
    }

    uint32_t Asking()
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0u;
    }

    const char* ModName(uint32_t mod)
    {
        const wxl::host::ModHost*   host = wxl::core::Host();
        const wxl::host::LoadedMod* owner = host ? host->ModOf(mod) : nullptr;
        return owner ? owner->id.c_str() : "the runtime";
    }

    // --- the table ----------------------------------------------------------------------------

    wxl_result WXL_CALL ServeTable(wxl_str table, wxl_str path)
    {
        char                   buffer[kKeyMax];
        const std::string_view key = KeyInto(table.data, table.size, buffer);
        if (key.empty() || !path.data || !path.size) return WXL_E_INVALID;

        const std::string file(path.data, path.size);
        if (!wxl::core::services::storage::Exists(file.c_str()))
        {
            WLOG_WARN(kTag, "%s cannot serve %s from %s: the client cannot read it",
                      ModName(Asking()), buffer, file.c_str());
            return WXL_E_NOT_FOUND;
        }

        const uint32_t mod = Asking();

        bool        taken = false;
        uint32_t    owner = 0;
        std::string from;
        bool        late = false;
        {
            Held held;
            if (const Served* already = Find(key))
            {
                taken = true;
                owner = already->mod;
                from  = already->path;
            }
            else
            {
                late = Loaded(key);
                g_served.push_back(Served{std::string(key), file, mod, late});
            }
        }

        if (taken)
        {
            WLOG_WARN(kTag, "%s cannot serve %s: %s already does, from %s", ModName(mod), buffer,
                      ModName(owner), from.c_str());
            return WXL_E_FAILED;
        }
        if (late)
            WLOG_WARN(kTag, "%s serves %s from %s, but the client read it already; it will be "
                            "used next session and not this one",
                      ModName(mod), buffer, file.c_str());
        else
            WLOG_INFO(kTag, "%s serves %s from %s", ModName(mod), buffer, file.c_str());
        return WXL_OK;
    }

    wxl_result WXL_CALL RestoreTable(wxl_str table)
    {
        char                   buffer[kKeyMax];
        const std::string_view key = KeyInto(table.data, table.size, buffer);
        if (key.empty()) return WXL_E_NOT_FOUND;

        const uint32_t mod    = Asking();
        wxl_result     answer = WXL_E_NOT_FOUND;
        {
            Held held;
            for (std::size_t i = 0; i < g_served.size(); ++i)
            {
                if (g_served[i].table != key) continue;
                // Only whoever claimed it may give it back: one mod cannot undo another's work
                // behind its back and leave it reading a table it does not expect.
                if (g_served[i].mod != mod)
                {
                    answer = WXL_E_FAILED;
                    break;
                }
                g_served.erase(g_served.begin() + static_cast<std::ptrdiff_t>(i));
                answer = WXL_OK;
                break;
            }
        }

        if (answer == WXL_OK) WLOG_INFO(kTag, "%s is the client's own again", buffer);
        return answer;
    }

    wxl_str WXL_CALL ServedFrom(wxl_str table)
    {
        char                   buffer[kKeyMax];
        const std::string_view key = KeyInto(table.data, table.size, buffer);

        // A copy, taken under the lock: the entry can be given back from another thread the
        // moment the lock drops, and the answer must outlive that.
        thread_local std::string answer;
        {
            Held          held;
            const Served* served = Find(key);
            if (!served) return wxl_str{nullptr, 0};
            answer = served->path;
        }
        return wxl_str{answer.c_str(), static_cast<uint32_t>(answer.size())};
    }

    wxl_bool WXL_CALL IsLoaded(wxl_str table)
    {
        char                   buffer[kKeyMax];
        const std::string_view key = KeyInto(table.data, table.size, buffer);

        Held held;
        return Loaded(key) ? WXL_TRUE : WXL_FALSE;
    }

    const wxl_data_api g_data = {
        sizeof(wxl_data_api), &ServeTable, &RestoreTable, &ServedFrom, &IsLoaded,
    };
}

namespace wxl::core::services
{
    const wxl_data_api& DataApi() { return g_data; }

    namespace data
    {
        bool ServedPath(const char* path, size_t size, char* out, size_t capacity)
        {
            if (!path || !out || !capacity) return false;

            // The suffix is read off the path as it came, before anything is built. This runs
            // at every file the client opens, and all but a few hundred of them are not
            // databases: what the rest of this function costs is what the test spares them.
            if (size < 5) return false;
            const char* ext = path + size - 4;
            if (ext[0] != '.' || Lower(ext[1]) != 'd' || Lower(ext[2]) != 'b'
                || Lower(ext[3]) != 'c')
                return false;

            char                   buffer[kKeyMax];
            const std::string_view key = KeyInto(path, size, buffer);
            if (key.empty()) return false;

            bool served  = false;
            bool tooLong = false;
            {
                Held held;

                // Every database the client opens is noted, whether or not anyone serves it:
                // that is what lets a mod be told its override came too late.
                if (!Loaded(key)) g_loaded.emplace_back(key);

                if (Served* table = Find(key))
                {
                    table->used = true;
                    // Copied out rather than handed over: the mod serving it may give the
                    // table back from another thread the moment the lock drops, and the client
                    // reads the path after this has returned.
                    if (table->path.size() < capacity)
                    {
                        std::memcpy(out, table->path.c_str(), table->path.size() + 1);
                        served = true;
                    }
                    else
                        tooLong = true;
                }
            }

            if (tooLong)
                WLOG_WARN(kTag, "%s is served from a path too long to hand to the client", buffer);
            return served;
        }

        void ForgetMod(uint32_t mod)
        {
            Held held;
            for (size_t i = g_served.size(); i > 0; --i)
            {
                if (g_served[i - 1].mod != mod) continue;
                WLOG_INFO(kTag, "%s is the client's own again: the mod serving it is gone",
                          g_served[i - 1].table.c_str());
                g_served.erase(g_served.begin() + static_cast<std::ptrdiff_t>(i - 1));
            }
        }

        void Clear()
        {
            Held held;
            g_served.clear();
            g_loaded.clear();
        }
    }
}
