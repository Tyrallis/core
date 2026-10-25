// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The hook engine: detour chains over MinHook, attached and detached one link at a time.

#include "hook/Engine.hpp"

#include "common/Log.hpp"
#include "hook/Safety.hpp"

#include <MinHook.h>
#include <windows.h>

#include <cstdio>
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
            uint8_t*          stub       = nullptr; ///< `jmp [&head]`, what the patch jumps to
            bool              patched    = false;   ///< the trampoline exists
            Backend           backend    = Backend::MinHook;
            size_t            length     = 0;       ///< the function's, for SafetyHook's check
            safety::Inline*   safety     = nullptr; ///< a SafetyHook chain's detour, armed while it has links
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
        bool Patch(Chain& chain, char* why, size_t capacity)
        {
            if (chain.patched)
            {
                // A SafetyHook chain that lost its last link was disarmed: the new one arms it.
                if (chain.safety && !safety::Arm(chain.safety, why, capacity))
                {
                    WLOG_ERROR(kTag, "arming @0x%p failed: %s", chain.target, why ? why : "");
                    return false;
                }
                return true;
            }

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

            if (chain.backend == Backend::SafetyHook)
            {
                chain.safety = safety::Create(chain.target, chain.length, chain.stub, &chain.trampoline, why, capacity);
                if (!chain.safety) return false;
                chain.patched = true;
                Relink(chain); // the head is in place before the address is live
                if (!safety::Arm(chain.safety, why, capacity)) return false;
                return true;
            }

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

    Id Attach(const char* name, void* target, void* detour, void** original, int priority, Backend backend,
              size_t length, char* why, size_t whyCapacity)
    {
        char spare[192];
        if (!why || !whyCapacity)
        {
            why         = spare;
            whyCapacity = sizeof spare;
        }
        why[0] = '\0';
        if (!name) name = "?";
        if (!target || !detour || !original)
        {
            WLOG_ERROR(kTag, "attach '%s': null target, detour or original", name);
            snprintf(why, whyCapacity, "null target, detour or original");
            return kNone;
        }

        std::lock_guard<std::recursive_mutex> guard(Lock());
        if (!g_initialised && !Init()) return kNone;

        Chain* chain = FindChain(target);
        if (!chain)
        {
            Chains().push_back(Chain{target});
            chain          = &Chains().back();
            chain->backend = backend;
            chain->length  = length;
        }

        // Equal priorities keep attach order: the new link goes after them.
        auto at = chain->links.end();
        while (at != chain->links.begin() && std::prev(at)->priority > priority) --at;

        const Id id = ++g_next;
        chain->links.insert(at, Link{id, name, detour, original, priority});
        Relink(*chain);

        if (!Patch(*chain, why, whyCapacity))
        {
            size_t index = 0;
            if (FindLink(id, index) == chain) chain->links.erase(chain->links.begin() + index);
            Relink(*chain);
            WLOG_ERROR(kTag, "attach '%s' @0x%p refused%s%s", name, target, why[0] ? ": " : "", why);
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
        if (chain->links.empty() && chain->safety)
        {
            // Nobody is left on it: the function gets its bytes back and runs undetoured.
            char why[192] = "";
            if (!safety::Disarm(chain->safety, why, sizeof why))
                WLOG_WARN(kTag, "disarming @0x%p failed (%s); it stays detoured and passes through",
                          chain->target, why);
        }
        WLOG_INFO(kTag, "detached '%s' @0x%p (%zu left on this address)", link.name, chain->target,
                  chain->links.size());
        return true;
    }

    bool Armed(void* target)
    {
        std::lock_guard<std::recursive_mutex> guard(Lock());
        const Chain* chain = FindChain(target);
        if (!chain || !chain->patched) return false;
        return !chain->safety || !chain->links.empty();
    }

    size_t Count(void* target)
    {
        std::lock_guard<std::recursive_mutex> guard(Lock());
        const Chain* chain = FindChain(target);
        return chain ? chain->links.size() : 0;
    }
}
