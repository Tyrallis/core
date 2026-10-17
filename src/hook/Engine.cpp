// The hook engine: detour chains over MinHook, attached and detached one link at a time.
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

#include "hook/Engine.hpp"

#include "common/Log.hpp"

#include <MinHook.h>
#include <windows.h>

#include <cstring>
#include <deque>
#include <iterator>
#include <mutex>
#include <vector>

namespace wxl::hook
{
    namespace
    {
        constexpr char kTag[] = "hook";

        /// One party on an address. `original` is the party's own variable; the chain rewrites
        /// it whenever the chain's shape changes.
        struct Link
        {
            Id          id;
            const char* name;
            void*       detour;
            void**      original;
            int         priority;
        };

        /// Every party on one address, head first.
        struct Chain
        {
            void*             target;
            std::vector<Link> links;
            void*             trampoline = nullptr; ///< MinHook's, runs the engine function
            void*             head       = nullptr; ///< what the stub jumps to: links[0].detour or the trampoline
            uint8_t*          stub       = nullptr; ///< `jmp [&head]`, what MinHook's patch jumps to
            bool              patched    = false;
        };

        /// deque rather than vector: the stub embeds the address of a Chain's `head`, so a chain
        /// must never move once created.
        std::deque<Chain>& Chains()
        {
            static std::deque<Chain> chains;
            return chains;
        }

        std::recursive_mutex& Lock()
        {
            static std::recursive_mutex lock;
            return lock;
        }

        Id   g_next        = kNone;
        bool g_initialised = false;

        /**
         * @brief Hands out six-byte executable stubs out of pages allocated on demand.
         * @return the stub, or null when no page can be had.
         */
        uint8_t* NewStub()
        {
            static uint8_t* page = nullptr;
            static size_t   used = 0;
            constexpr size_t kPage = 4096, kStub = 8;
            if (!page || used + kStub > kPage)
            {
                page = static_cast<uint8_t*>(VirtualAlloc(nullptr, kPage, MEM_COMMIT | MEM_RESERVE,
                                                          PAGE_EXECUTE_READWRITE));
                used = 0;
                if (!page) return nullptr;
            }
            uint8_t* stub = page + used;
            used += kStub;
            return stub;
        }

        Chain* FindChain(void* target)
        {
            for (Chain& chain : Chains())
                if (chain.target == target) return &chain;
            return nullptr;
        }

        Chain* FindLink(Id id, size_t& index)
        {
            for (Chain& chain : Chains())
                for (size_t i = 0; i < chain.links.size(); ++i)
                    if (chain.links[i].id == id) { index = i; return &chain; }
            return nullptr;
        }

        /// Points every party at its successor, the last one at the engine function, and the
        /// stub's slot at the first party.
        void Relink(Chain& chain)
        {
            const size_t count = chain.links.size();
            for (size_t i = 0; i + 1 < count; ++i)
                *chain.links[i].original = chain.links[i + 1].detour;
            if (!chain.patched) return;
            if (count) *chain.links[count - 1].original = chain.trampoline;
            chain.head = count ? chain.links[0].detour : chain.trampoline;
        }

        /**
         * @brief Patches the target for the first time: stub, MinHook hook, enable.
         * @return true when the chain is live.
         */
        bool Patch(Chain& chain)
        {
            if (chain.patched) return true;

            chain.stub = NewStub();
            if (!chain.stub)
            {
                WLOG_ERROR(kTag, "no executable page for a stub (win32=%lu)", GetLastError());
                return false;
            }
            // jmp dword ptr [&chain.head]
            chain.stub[0] = 0xFF;
            chain.stub[1] = 0x25;
            void** slot   = &chain.head;
            memcpy(chain.stub + 2, &slot, sizeof slot);
            chain.head = nullptr; // set by Relink once the trampoline exists

            MH_STATUS status = MH_CreateHook(chain.target, chain.stub, &chain.trampoline);
            if (status != MH_OK)
            {
                WLOG_ERROR(kTag, "create @0x%p failed (%s)", chain.target, MH_StatusToString(status));
                return false;
            }
            chain.patched = true;
            Relink(chain); // the head is in place before the address is live

            status = MH_EnableHook(chain.target);
            if (status != MH_OK)
            {
                WLOG_ERROR(kTag, "enable @0x%p failed (%s)", chain.target, MH_StatusToString(status));
                MH_RemoveHook(chain.target);
                chain.patched    = false;
                chain.trampoline = nullptr;
                return false;
            }
            return true;
        }
    }

    bool Init()
    {
        std::lock_guard<std::recursive_mutex> guard(Lock());
        if (g_initialised) return true;
        const MH_STATUS status = MH_Initialize();
        if (status != MH_OK)
        {
            WLOG_ERROR(kTag, "MH_Initialize failed (%s)", MH_StatusToString(status));
            return false;
        }
        g_initialised = true;
        return true;
    }

    Id Attach(const char* name, void* target, void* detour, void** original, int priority)
    {
        if (!name) name = "?";
        if (!target || !detour || !original)
        {
            WLOG_ERROR(kTag, "attach '%s': null target, detour or original", name);
            return kNone;
        }

        std::lock_guard<std::recursive_mutex> guard(Lock());
        if (!g_initialised && !Init()) return kNone;

        Chain* chain = FindChain(target);
        if (!chain)
        {
            Chains().push_back(Chain{target});
            chain = &Chains().back();
        }

        // Equal priorities keep attach order: the new link goes after them.
        auto at = chain->links.end();
        while (at != chain->links.begin() && std::prev(at)->priority > priority) --at;

        const Id id = ++g_next;
        chain->links.insert(at, Link{id, name, detour, original, priority});
        Relink(*chain);

        if (!Patch(*chain))
        {
            size_t index = 0;
            if (FindLink(id, index) == chain) chain->links.erase(chain->links.begin() + index);
            Relink(*chain);
            WLOG_ERROR(kTag, "attach '%s' @0x%p refused", name, target);
            return kNone;
        }

        WLOG_INFO(kTag, "attached '%s' @0x%p (priority %d, %zu on this address)", name, target,
                  priority, chain->links.size());
        return id;
    }

    bool Detach(Id id)
    {
        std::lock_guard<std::recursive_mutex> guard(Lock());
        size_t index = 0;
        Chain* chain = FindLink(id, index);
        if (!chain) return false;

        const Link link = chain->links[index];
        chain->links.erase(chain->links.begin() + index);
        Relink(*chain);
        WLOG_INFO(kTag, "detached '%s' @0x%p (%zu left on this address)", link.name, chain->target,
                  chain->links.size());
        return true;
    }

    size_t Count(void* target)
    {
        std::lock_guard<std::recursive_mutex> guard(Lock());
        const Chain* chain = FindChain(target);
        return chain ? chain->links.size() : 0;
    }
}
