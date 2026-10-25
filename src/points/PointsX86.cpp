// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.point on the stock client: a generated detour per point, armed at its first handler.

#include "points/PointsX86.hpp"

#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "hook/Safety.hpp"
#include "points/DetourX86.hpp"
#include "runtime/Image.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace wxl::core::points
{
    namespace
    {
        constexpr char kTag[] = "point";

        namespace safety = ::wxl::hook::safety;

        /// In front of every seam and wxl.hook link on the address: the point's Replace decides
        /// whether they run at all, as the WXL_POINT line before the seam's call does on x64.
        constexpr int kPriority = INT_MIN;

#define WXL_POINT_X86(Ident, name, address, length, conv, ret, params) \
    using Detour_##Ident = DetourOf<k##Ident, ::wxl::seam::detail::conv_##conv, ret params>;
#define WXL_POINT_SITE(Ident, address, args)
#define WXL_POINT_X86_ABSENT(Ident, why)
#include "wxl/points/x86.def"
#undef WXL_POINT_X86_ABSENT
#undef WXL_POINT_SITE
#undef WXL_POINT_X86

        struct Detoured
        {
            uint32_t    index;
            uintptr_t   address; ///< in Wow.exe 12340
            size_t      length;
            void*       detour;
            void**      original;
        };

        Detoured g_rows[] = {
#define WXL_POINT_X86(Ident, name, address, length, conv, ret, params)                   \
    {k##Ident, address, length, reinterpret_cast<void*>(&Detour_##Ident::Detour),        \
     reinterpret_cast<void**>(&Detour_##Ident::original)},
#define WXL_POINT_SITE(Ident, address, args)
#define WXL_POINT_X86_ABSENT(Ident, why)
#include "wxl/points/x86.def"
#undef WXL_POINT_X86_ABSENT
#undef WXL_POINT_SITE
#undef WXL_POINT_X86
        };

        struct Absent
        {
            uint32_t    index;
            const char* why;
        };

        const Absent kAbsent[] = {
            {kCount, ""},
#define WXL_POINT_X86(Ident, name, address, length, conv, ret, params)
#define WXL_POINT_SITE(Ident, address, args)
#define WXL_POINT_X86_ABSENT(Ident, why) {k##Ident, why},
#include "wxl/points/x86.def"
#undef WXL_POINT_X86_ABSENT
#undef WXL_POINT_SITE
#undef WXL_POINT_X86
        };

        // --- Sites: inlined copies, reached by a mid-function hook -------------------------

        /// Where an argument lives at a site: a register of the context, or a stack slot off one.
        struct Source
        {
            size_t   reg    = 0;     ///< offsetof(Registers, <reg>)
            bool     memory = false; ///< [reg + offset] rather than reg
            intptr_t offset = 0;
        };

        constexpr size_t kMaxArgs = 16;

        struct Site
        {
            uint32_t     index;
            uintptr_t    address;
            const char*  args;               ///< "ecx,[esp+8]"
            uint32_t     count = 0;
            Source       sources[kMaxArgs];
            safety::Mid* hook = nullptr;
        };

        std::vector<Site>& Sites()
        {
            static std::vector<Site> sites = [] {
                std::vector<Site> made;
#define WXL_POINT_X86(Ident, name, address, length, conv, ret, params)
#define WXL_POINT_SITE(Ident, address, args) made.push_back(Site{k##Ident, address, args});
#define WXL_POINT_X86_ABSENT(Ident, why)
#include "wxl/points/x86.def"
#undef WXL_POINT_X86_ABSENT
#undef WXL_POINT_SITE
#undef WXL_POINT_X86
                return made;
            }();
            return sites;
        }

        bool Register(const char* name, size_t length, size_t& offset)
        {
            static const struct
            {
                const char* name;
                size_t      offset;
            } kRegisters[] = {
                {"eax", offsetof(safety::Registers, eax)}, {"ecx", offsetof(safety::Registers, ecx)},
                {"edx", offsetof(safety::Registers, edx)}, {"ebx", offsetof(safety::Registers, ebx)},
                {"esi", offsetof(safety::Registers, esi)}, {"edi", offsetof(safety::Registers, edi)},
                {"ebp", offsetof(safety::Registers, ebp)}, {"esp", offsetof(safety::Registers, esp)},
            };
            for (const auto& reg : kRegisters)
                if (length == 3 && strncmp(reg.name, name, 3) == 0)
                {
                    offset = reg.offset;
                    return true;
                }
            return false;
        }

        bool Parse(Site& site)
        {
            site.count = 0;
            for (const char* at = site.args; *at;)
            {
                if (site.count == kMaxArgs) return false;
                const char* end = strchr(at, ',');
                if (!end) end = at + strlen(at);
                Source& source = site.sources[site.count++];
                if (*at == '[')
                {
                    source.memory = true;
                    if (!Register(at + 1, 3, source.reg)) return false;
                    const char sign = at[4];
                    if (sign != '+' && sign != '-') return false;
                    source.offset = static_cast<intptr_t>(strtol(at + 5, nullptr, 0)) * (sign == '-' ? -1 : 1);
                }
                else if (!Register(at, static_cast<size_t>(end - at), source.reg))
                    return false;
                at = *end ? end + 1 : end;
            }
            return true;
        }

        /// A mid-function hook is handed the registers and nothing else, so each site gets a
        /// function of its own: one instance per position in Sites(), up to kSiteSlots.
        constexpr size_t kSiteSlots = 64;

        template <size_t S>
        void SiteHit(safety::Registers& registers)
        {
            const Site& site = Sites()[S];
            void*       args[kMaxArgs];
            for (uint32_t i = 0; i < site.count; ++i)
            {
                const Source& source = site.sources[i];
                uintptr_t*    reg    = reinterpret_cast<uintptr_t*>(reinterpret_cast<char*>(&registers) + source.reg);
                args[i]              = source.memory ? reinterpret_cast<void*>(*reg + source.offset) : reg;
            }
            EnterInlined(site.index, args);
        }

        template <size_t... S>
        constexpr std::array<safety::MidFn, sizeof...(S)> SiteFns(std::index_sequence<S...>)
        {
            return {&SiteHit<S>...};
        }

        constexpr std::array<safety::MidFn, kSiteSlots> kSiteFns = SiteFns(std::make_index_sequence<kSiteSlots>{});

        // --- The rows of this runtime -----------------------------------------------------

        Detoured* g_byIndex[kCount > 0 ? kCount : 1] = {};
        wxl::hook::Id g_links[kCount > 0 ? kCount : 1] = {};
        bool          g_placed[kCount > 0 ? kCount : 1] = {};

        void IndexRows()
        {
            static bool done = false;
            if (done) return;
            done = true;
            for (Detoured& row : g_rows) g_byIndex[row.index] = &row;
        }

        bool ArmSites(uint32_t index, char* why, size_t capacity)
        {
            for (size_t slot = 0; slot < Sites().size(); ++slot)
            {
                Site& site = Sites()[slot];
                if (site.index != index) continue;
                if (slot >= kSiteSlots)
                {
                    snprintf(why, capacity, "it has a site past the %zu the runtime serves", kSiteSlots);
                    return false;
                }
                if (!site.hook)
                {
                    if (!Parse(site) || site.count != RowOf(index).arg_count)
                    {
                        snprintf(why, capacity, "its site at 0x%08zX names its arguments as `%s`, which is not %u "
                                                "register(s) or stack slot(s)",
                                 static_cast<size_t>(site.address), site.args, RowOf(index).arg_count);
                        return false;
                    }
                    site.hook = safety::CreateMid(reinterpret_cast<void*>(site.address), kSiteFns[slot], why, capacity);
                    if (!site.hook) return false;
                }
                if (!safety::ArmMid(site.hook, why, capacity)) return false;
            }
            return true;
        }

        void DisarmSites(uint32_t index)
        {
            for (Site& site : Sites())
            {
                if (site.index != index || !site.hook) continue;
                char why[160] = "";
                if (!safety::DisarmMid(site.hook, why, sizeof why))
                    WLOG_WARN(kTag, "%s: its site at 0x%08zX stays hooked: %s", RowOf(index).name,
                              static_cast<size_t>(site.address), why);
            }
        }
    }

    bool Has(uint32_t index, const char** why)
    {
        IndexRows();
        if (index >= kCount) return false;
        if (!g_byIndex[index])
        {
            if (why)
            {
                *why = "x64 only";
                for (const Absent& absent : kAbsent)
                    if (absent.index == index) *why = absent.why;
            }
            return false;
        }
        if (!g_placed[index] && !image::IsClient())
        {
            if (why) *why = "the executable is not Wow.exe build 12340, whose addresses the points are";
            return false;
        }
        return true;
    }

    bool Arm(uint32_t index, char* why, size_t capacity)
    {
        IndexRows();
        Detoured* row = index < kCount ? g_byIndex[index] : nullptr;
        if (!row)
        {
            snprintf(why, capacity, "it has no x86 detour");
            return false;
        }
        const wxl::hook::Id link =
            wxl::hook::Attach(RowOf(index).name, reinterpret_cast<void*>(row->address), row->detour, row->original,
                              kPriority, wxl::hook::Backend::SafetyHook, row->length, why, capacity);
        if (link == wxl::hook::kNone) return false;
        if (!ArmSites(index, why, capacity))
        {
            wxl::hook::Detach(link);
            DisarmSites(index);
            return false;
        }
        g_links[index] = link;
        return true;
    }

    void Disarm(uint32_t index)
    {
        if (index >= kCount || g_links[index] == wxl::hook::kNone) return;
        wxl::hook::Detach(g_links[index]);
        g_links[index] = wxl::hook::kNone;
        DisarmSites(index);
    }

    namespace x86
    {
        void Place(uint32_t index, uintptr_t address, size_t length)
        {
            IndexRows();
            if (index >= kCount || !g_byIndex[index]) return;
            g_byIndex[index]->address = address;
            g_byIndex[index]->length  = length;
            g_placed[index]           = true;
        }

        void AddSite(uint32_t index, uintptr_t address, const char* args)
        {
            Sites().push_back(Site{index, address, args});
        }

        size_t SiteCount() { return Sites().size(); }

        void* DetourOf(uint32_t index)
        {
            IndexRows();
            return index < kCount && g_byIndex[index] ? g_byIndex[index]->detour : nullptr;
        }
    }
}
