// The wxl.data service: which client databases a mod serves from a file of its own.
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

#include "services/DataService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"

#include <cstddef>
#include <string>
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

    std::vector<Served>      g_served;
    std::vector<std::string> g_loaded; ///< every table the client has read, basename lowercased

    char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

    /**
     * @brief A table's name reduced to what identifies it: the file, lowercased.
     *
     * The client names a table by its full path, "DBFilesClient\Spell.dbc", and a mod is
     * likely to write just "Spell.dbc". Database names are unique across the client, so the
     * folder carries no information and dropping it makes both spellings the same table.
     */
    std::string Key(const char* text, size_t size)
    {
        if (!text) return std::string();
        size_t start = 0;
        for (size_t i = 0; i < size; ++i)
            if (text[i] == '\\' || text[i] == '/') start = i + 1;

        std::string key;
        key.reserve(size - start);
        for (size_t i = start; i < size; ++i) key.push_back(Lower(text[i]));
        return key;
    }

    std::string Key(wxl_str text) { return Key(text.data, text.size); }

    Served* Find(const std::string& key)
    {
        for (Served& served : g_served)
            if (served.table == key) return &served;
        return nullptr;
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
        const std::string key = Key(table);
        if (key.empty() || !path.data || !path.size) return WXL_E_INVALID;

        const std::string file(path.data, path.size);
        if (!wxl::core::binding::file_exists(file.c_str()))
        {
            WLOG_WARN(kTag, "%s cannot serve %s from %s: the client cannot read it",
                      ModName(Asking()), key.c_str(), file.c_str());
            return WXL_E_NOT_FOUND;
        }

        if (const Served* taken = Find(key))
        {
            WLOG_WARN(kTag, "%s cannot serve %s: %s already does, from %s", ModName(Asking()),
                      key.c_str(), ModName(taken->mod), taken->path.c_str());
            return WXL_E_FAILED;
        }

        const bool late = std::find(g_loaded.begin(), g_loaded.end(), key) != g_loaded.end();
        g_served.push_back(Served{key, file, Asking(), late});
        if (late)
            WLOG_WARN(kTag, "%s serves %s from %s, but the client read it already; it will be "
                            "used next session and not this one",
                      ModName(Asking()), key.c_str(), file.c_str());
        else
            WLOG_INFO(kTag, "%s serves %s from %s", ModName(Asking()), key.c_str(), file.c_str());
        return WXL_OK;
    }

    wxl_result WXL_CALL RestoreTable(wxl_str table)
    {
        const std::string key = Key(table);
        for (size_t i = 0; i < g_served.size(); ++i)
        {
            if (g_served[i].table != key) continue;
            // Only whoever claimed it may give it back: one mod cannot undo another's work
            // behind its back and leave it reading a table it does not expect.
            if (g_served[i].mod != Asking()) return WXL_E_FAILED;
            g_served.erase(g_served.begin() + static_cast<std::ptrdiff_t>(i));
            WLOG_INFO(kTag, "%s is the client's own again", key.c_str());
            return WXL_OK;
        }
        return WXL_E_NOT_FOUND;
    }

    wxl_str WXL_CALL ServedFrom(wxl_str table)
    {
        const Served* served = Find(Key(table));
        if (!served) return wxl_str{nullptr, 0};
        return wxl_str{served->path.c_str(), static_cast<uint32_t>(served->path.size())};
    }

    wxl_bool WXL_CALL IsLoaded(wxl_str table)
    {
        const std::string key = Key(table);
        return std::find(g_loaded.begin(), g_loaded.end(), key) != g_loaded.end() ? WXL_TRUE
                                                                                   : WXL_FALSE;
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
        const char* ServedPath(const char* path, size_t size)
        {
            if (!path) return nullptr;
            const std::string key = Key(path, size);
            if (key.size() < 5 || key.compare(key.size() - 4, 4, ".dbc") != 0) return nullptr;

            // Every database the client opens is noted, whether or not anyone serves it: that
            // is what lets a mod be told its override came too late.
            if (std::find(g_loaded.begin(), g_loaded.end(), key) == g_loaded.end())
                g_loaded.push_back(key);

            Served* served = Find(key);
            if (!served) return nullptr;
            served->used = true;
            return served->path.c_str();
        }

        void ForgetMod(uint32_t mod)
        {
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
            g_served.clear();
            g_loaded.clear();
        }
    }
}
