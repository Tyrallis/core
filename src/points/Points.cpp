// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.point: the handlers attached to the engine's functions, and the dispatch that runs them.

#include "points/Points.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/guard.hpp"
#include "wxl/host/mod_host.hpp"

#include <ankerl/unordered_dense.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace wxl::core::points
{
    namespace
    {
        constexpr char kTag[] = "point";

#define WXL_POINT_FLAGS_replace     WXL_POINT_REPLACEABLE
#define WXL_POINT_FLAGS_observe     0u
#define WXL_POINT_FLAGS_stable      WXL_POINT_STABLE
#define WXL_POINT_FLAGS_provisional 0u
#define WXL_POINT_FLAGS_both        (WXL_POINT_ON_X86 | WXL_POINT_ON_X64)
#define WXL_POINT_FLAGS_x86         WXL_POINT_ON_X86
#define WXL_POINT_FLAGS_x64         WXL_POINT_ON_X64
#define WXL_POINT_THREAD_main       WXL_POINT_THREAD_MAIN
#define WXL_POINT_THREAD_any        WXL_POINT_THREAD_ANY

        template <class Sig>
        struct Shape;
        template <class R, class... A>
        struct Shape<R(A...)>
        {
            static constexpr uint32_t kArgs    = sizeof...(A);
            static constexpr bool     kReturns = !std::is_void_v<R>;
        };

        const Row kRows[] = {
#define WXL_POINT(Ident, name, ret, params, lowered, layout_hash, policy, thread, stability, runtimes)       \
    Row{name, lowered, layout_hash, WXL_POINT_THREAD_##thread,                                              \
        WXL_POINT_FLAGS_##policy | WXL_POINT_FLAGS_##stability | WXL_POINT_FLAGS_##runtimes,                \
        Shape<ret params>::kArgs, Shape<ret params>::kReturns},
#include "wxl/points.def"
#undef WXL_POINT
        };
        static_assert(sizeof kRows / sizeof kRows[0] == kCount, "one row per point");

        /// One handler on one side of one point.
        struct Link
        {
            uint32_t          id;
            uint32_t          owner;    ///< the host's token for its mod, 0 for the runtime's own
            uint32_t          index;
            uint32_t          side;
            wxl_point_fn      fn;
            void*             user;
            int32_t           priority;
            uint64_t          order;    ///< attach order, the last tie-break
            std::atomic<bool> live{true};
        };

        /// A point's handlers, each side in run order. Never changed once published.
        struct Lists
        {
            std::vector<Link*> side[3];

            bool Empty() const { return side[0].empty() && side[1].empty() && side[2].empty(); }
        };

        struct Slot
        {
            std::atomic<const Lists*> lists{nullptr}; ///< what a call reads; null with nothing attached
            std::unique_ptr<Lists>    current;        ///< owns what lists points at
            uint32_t                  count = 0;      ///< handlers attached, all sides
        };

        Slot g_slots[kCount];

        std::mutex                                                 g_write; ///< every change; no call holds it
        ankerl::unordered_dense::map<uint32_t, std::unique_ptr<Link>> g_links;
        std::vector<std::unique_ptr<Lists>>                        g_retiredLists;
        std::vector<std::unique_ptr<Link>>                         g_retiredLinks;
        std::atomic<uint32_t>                                      g_inFlight{0};
        uint32_t                                                   g_nextId    = 0;
        uint64_t                                                   g_nextOrder = 0;

        /// How deep each point is on this thread: 1 inside its outermost call.
        thread_local uint32_t t_depth[kCount > 0 ? kCount : 1] = {};

        /// The pairs of Replace handlers already told one shadows the other.
        std::mutex                                g_warnedLock;
        ankerl::unordered_dense::set<uint64_t>    g_warned;

        std::string Who(uint32_t owner)
        {
            if (owner == 0) return "the runtime";
            const wxl::host::ModHost* host = wxl::core::Host();
            const wxl::host::LoadedMod* mod = host ? host->ModOf(owner) : nullptr;
            return mod ? "mod " + mod->id : "mod " + std::to_string(owner);
        }

        const char* SideName(uint32_t side)
        {
            switch (side)
            {
            case WXL_POINT_BEFORE: return "Before";
            case WXL_POINT_REPLACE: return "Replace";
            case WXL_POINT_AFTER: return "After";
            }
            return "?";
        }

        /// Run order: lower priority first among Befores and outermost among Replaces, last among
        /// Afters, so that one mod's pair of handlers nests around the others'; ties by load
        /// order (the owner's token), then attach order.
        bool Before(const Link* a, const Link* b)
        {
            if (a->priority != b->priority) return a->priority < b->priority;
            if (a->owner != b->owner) return a->owner < b->owner;
            return a->order < b->order;
        }

        void Reclaim()
        {
            // Every call counts itself in before it reads a list, and every publish lands before
            // this load: with nobody counted in, nothing retired is still read.
            if (g_inFlight.load(std::memory_order_seq_cst) != 0) return;
            g_retiredLists.clear();
            g_retiredLinks.clear();
        }

        /// Replaces a point's lists. Holds g_write.
        void Publish(Slot& slot, std::unique_ptr<Lists> next)
        {
            if (next && next->Empty()) next.reset();
            std::unique_ptr<Lists> old = std::move(slot.current);
            slot.current               = std::move(next);
            slot.lists.store(slot.current.get(), std::memory_order_seq_cst);
            if (old) g_retiredLists.push_back(std::move(old));
            Reclaim();
        }

        /// Takes a link out of its point. Holds g_write.
        void Unlink(Link* link)
        {
            link->live.store(false, std::memory_order_release);
            Slot&                  slot = g_slots[link->index];
            std::unique_ptr<Lists> next(slot.current ? new Lists(*slot.current) : new Lists);
            std::vector<Link*>&    list = next->side[link->side];
            list.erase(std::remove(list.begin(), list.end(), link), list.end());
            Publish(slot, std::move(next));
            if (slot.count && --slot.count == 0)
            {
                Disarm(link->index);
                WLOG_INFO(kTag, "%s: its last handler went; unhooked", kRows[link->index].name);
            }
            auto found = g_links.find(link->id);
            if (found != g_links.end())
            {
                g_retiredLinks.push_back(std::move(found->second));
                g_links.erase(found);
            }
        }

        // --- Running a handler -------------------------------------------------------------

        struct Frame
        {
            uint32_t      index;
            const Lists*  lists;
            void* const*  args;
            void*         result;
            Original      original;
        };

        struct Invocation
        {
            Link*           link;
            wxl_point_call* call;
        };

        void Invoke(void* context)
        {
            Invocation* invocation = static_cast<Invocation*>(context);
            try
            {
                invocation->link->fn(invocation->call, invocation->link->user);
            }
            catch (const std::exception& e)
            {
                WLOG_ERROR(kTag, "%s's %s handler on %s threw: %s", Who(invocation->link->owner).c_str(),
                           SideName(invocation->link->side), kRows[invocation->link->index].name, e.what());
            }
            catch (...)
            {
                WLOG_ERROR(kTag, "%s's %s handler on %s threw something that is not a std::exception",
                           Who(invocation->link->owner).c_str(), SideName(invocation->link->side),
                           kRows[invocation->link->index].name);
            }
        }

        /// A mod whose handler faulted: its handlers stop at once, on every thread, and the host
        /// disables it (on the main thread, at once or at its next raise).
        void Faulted(Link* link, const wxl::host::Fault& fault)
        {
            char described[192];
            wxl::host::DescribeFault(fault, described, sizeof described);
            const uint32_t owner = link->owner;
            WLOG_ERROR(kTag, "%s's %s handler on %s faulted: %s; the call goes on", Who(owner).c_str(),
                       SideName(link->side), kRows[link->index].name, described);
            if (owner == 0)
            {
                // The runtime's own: that handler alone stops.
                link->live.store(false, std::memory_order_release);
            }
            else
            {
                std::lock_guard<std::mutex> lock(g_write);
                for (auto& [id, other] : g_links)
                    if (other->owner == owner) other->live.store(false, std::memory_order_release);
            }
            if (wxl::host::ModHost* host = wxl::core::Host(); host && owner)
                host->Disable(owner, std::string("in its ") + SideName(link->side) + " handler on point " +
                                         kRows[link->index].name + ": " + described);
        }

        /// Runs one handler under the crash guard, as its mod. False when it faulted.
        bool Run(Link* link, wxl_point_call* call)
        {
            Invocation             invocation{link, call};
            wxl::host::ModHost*    host  = wxl::core::Host();
            const uint32_t         saved = host ? host->SwapCurrentMod(link->owner) : 0;
            wxl::host::Fault       fault;
            const bool             returned = wxl::host::CallGuarded(&Invoke, &invocation, &fault);
            if (host) host->SwapCurrentMod(saved);
            if (!returned) Faulted(link, fault);
            return returned;
        }

        void WarnShadowed(const Link* outer, const Link* inner)
        {
            const uint64_t key = (static_cast<uint64_t>(outer->owner) << 40) ^ (static_cast<uint64_t>(inner->owner) << 16) ^ outer->index;
            {
                std::lock_guard<std::mutex> lock(g_warnedLock);
                if (!g_warned.insert(key).second) return;
            }
            WLOG_WARN(kTag, "%s's Replace on %s never calls the original; %s's Replace below it does not run",
                      Who(outer->owner).c_str(), kRows[outer->index].name, Who(inner->owner).c_str());
        }

        /// Runs the Replace chain of a call from link @p at down, then the original.
        void RunReplace(Frame& frame, size_t at, const wxl_point_call* from)
        {
            const std::vector<Link*>& chain = frame.lists->side[WXL_POINT_REPLACE];
            while (at < chain.size() && !chain[at]->live.load(std::memory_order_acquire)) ++at;
            if (at == chain.size())
            {
                frame.original(frame.args, frame.result);
                return;
            }
            wxl_point_call call = *from;
            call.side           = WXL_POINT_REPLACE;
            call.runtime[0]     = reinterpret_cast<uintptr_t>(&frame);
            call.runtime[1]     = at;
            call.runtime[2]     = 0;
            const bool returned = Run(chain[at], &call);
            if (call.runtime[2]) return; // it called the original: the rest of the chain ran
            if (!returned)
            {
                // It faulted before calling the original: the call goes on as if it had not been there.
                RunReplace(frame, at + 1, &call);
                return;
            }
            for (size_t below = at + 1; below < chain.size(); ++below)
                if (chain[below]->live.load(std::memory_order_acquire))
                {
                    WarnShadowed(chain[at], chain[below]);
                    break;
                }
        }

        struct InFlight
        {
            InFlight() { g_inFlight.fetch_add(1, std::memory_order_seq_cst); }
            ~InFlight() { g_inFlight.fetch_sub(1, std::memory_order_seq_cst); }
        };

        /// The last call out frees what changes made while it ran left behind.
        void Settle()
        {
            if (g_inFlight.load(std::memory_order_relaxed) != 0) return;
            std::unique_lock<std::mutex> lock(g_write, std::try_to_lock);
            if (lock && (!g_retiredLists.empty() || !g_retiredLinks.empty())) Reclaim();
        }

        void RunSide(const std::vector<Link*>& list, wxl_point_call& call)
        {
            for (Link* link : list)
                if (link->live.load(std::memory_order_acquire)) Run(link, &call);
        }

        // --- The table -------------------------------------------------------------------

        wxl_str Str(const char* text) { return wxl_str{text, static_cast<uint32_t>(text ? strlen(text) : 0)}; }

        uint32_t WXL_CALL List(wxl_point_info* out, uint32_t capacity)
        {
            if (out)
            {
                for (uint32_t i = 0; i < kCount && i < capacity; ++i)
                {
                    const Row&  row = kRows[i];
                    const char* why = "";
                    const bool  has = Has(i, &why);
                    wxl_point_info& info = out[i];
                    info.struct_size     = sizeof(wxl_point_info);
                    info.point           = has ? i + 1 : WXL_POINT_NONE;
                    info.name            = Str(row.name);
                    info.signature       = Str(row.lowered);
                    info.thread          = row.thread;
                    info.flags           = row.flags | (has ? WXL_POINT_HERE : 0u);
                    info.layout_hash     = row.layout_hash;
                    info.arg_count       = row.arg_count;
                    info.absent          = Str(has ? "" : why);
                }
            }
            return kCount;
        }

        wxl_result WXL_CALL Find(const char* name, uint32_t* out)
        {
            if (!name || !out) return WXL_E_INVALID;
            for (uint32_t i = 0; i < kCount; ++i)
            {
                if (strcmp(kRows[i].name, name) != 0) continue;
                const char* why = "";
                if (!Has(i, &why)) return WXL_E_UNSUPPORTED;
                *out = i + 1;
                return WXL_OK;
            }
            return WXL_E_NOT_FOUND;
        }

        wxl_point_attachment WXL_CALL Attach(uint32_t point, uint32_t side, wxl_point_fn handler, void* user,
                                             int32_t priority, uint32_t layoutHash)
        {
            const wxl::host::ModHost* host  = wxl::core::Host();
            const uint32_t            owner = host ? host->CurrentMod() : 0;
            if (point == WXL_POINT_NONE || point > kCount)
            {
                WLOG_WARN(kTag, "%s: attach to point %u refused: this runtime lists %u point(s), ids 1 to %u",
                          Who(owner).c_str(), point, static_cast<unsigned>(kCount), static_cast<unsigned>(kCount));
                return WXL_POINT_ATTACHMENT_NONE;
            }
            const uint32_t index = point - 1;
            const Row&     row   = kRows[index];
            const char*    absent = "";
            const char*    refusal = nullptr;
            char           buffer[384];
            if (!handler) refusal = "the handler is null";
            else if (side > WXL_POINT_AFTER) refusal = "the side is not WXL_POINT_BEFORE, _REPLACE or _AFTER";
            else if (!Has(index, &absent))
            {
                snprintf(buffer, sizeof buffer, "this runtime does not have it: %s", absent);
                refusal = buffer;
            }
            else if (side == WXL_POINT_REPLACE && !(row.flags & WXL_POINT_REPLACEABLE))
                refusal = "it is observe only: attach Before or After";
            else if (layoutHash && layoutHash != row.layout_hash)
            {
                snprintf(buffer, sizeof buffer,
                         "the mod was built against another signature of it (layout 0x%08X); this runtime's is %s "
                         "(0x%08X): rebuild the mod against this runtime's SDK",
                         layoutHash, row.lowered, row.layout_hash);
                refusal = buffer;
            }
            if (refusal)
            {
                WLOG_WARN(kTag, "%s: %s on %s refused: %s", Who(owner).c_str(), SideName(side), row.name, refusal);
                return WXL_POINT_ATTACHMENT_NONE;
            }

            std::lock_guard<std::mutex> lock(g_write);
            Slot& slot = g_slots[index];
            if (slot.count == 0)
            {
                char why[256] = "";
                if (!Arm(index, why, sizeof why))
                {
                    WLOG_WARN(kTag, "%s: %s on %s refused: it cannot be hooked here: %s", Who(owner).c_str(),
                              SideName(side), row.name, why);
                    return WXL_POINT_ATTACHMENT_NONE;
                }
                WLOG_INFO(kTag, "%s: hooked for its first handler", row.name);
            }

            uint32_t id = ++g_nextId;
            if (id == WXL_POINT_ATTACHMENT_NONE) id = ++g_nextId;
            std::unique_ptr<Link> made(new Link{id, owner, index, side, handler, user, priority, ++g_nextOrder});
            Link* const           link = made.get();
            g_links.emplace(id, std::move(made));

            std::unique_ptr<Lists> next(slot.current ? new Lists(*slot.current) : new Lists);
            std::vector<Link*>&    list = next->side[side];
            if (side == WXL_POINT_AFTER)
                list.insert(std::upper_bound(list.begin(), list.end(), link,
                                             [](const Link* a, const Link* b) { return Before(b, a); }),
                            link);
            else
                list.insert(std::upper_bound(list.begin(), list.end(), link, &Before), link);
            Publish(slot, std::move(next));
            ++slot.count;
            WLOG_INFO(kTag, "%s: %s attached a %s handler (priority %d); %u on it", row.name, Who(owner).c_str(),
                      SideName(side), priority, slot.count);
            return id;
        }

        wxl_result WXL_CALL Detach(uint32_t attachment)
        {
            std::lock_guard<std::mutex> lock(g_write);
            auto found = g_links.find(attachment);
            if (found == g_links.end()) return WXL_E_NOT_FOUND;
            Unlink(found->second.get());
            return WXL_OK;
        }

        void WXL_CALL CallOriginal(wxl_point_call* call)
        {
            if (!call || call->side != WXL_POINT_REPLACE || !call->runtime[0])
            {
                static std::atomic<bool> told{false};
                if (!told.exchange(true))
                    WLOG_WARN(kTag, "call_original was called with a call that is not a Replace handler's; "
                                    "it does nothing (only a Replace has an original to call)");
                return;
            }
            Frame& frame     = *reinterpret_cast<Frame*>(call->runtime[0]);
            call->runtime[2] = 1;
            RunReplace(frame, static_cast<size_t>(call->runtime[1]) + 1, call);
        }

        const wxl_point_api g_api = {
            sizeof(wxl_point_api), &List, &Find, &Attach, &Detach, &CallOriginal,
        };
    }

    const Row& RowOf(uint32_t index) { return kRows[index]; }

    const wxl_point_api& Api() { return g_api; }

    void Enter(uint32_t index, void* const* args, void* result, Original original)
    {
        if (index >= kCount || !g_slots[index].lists.load(std::memory_order_relaxed))
        {
            original(args, result);
            return;
        }
        {
            const InFlight counted;
            const Lists*   lists = g_slots[index].lists.load(std::memory_order_seq_cst);
            if (!lists)
            {
                original(args, result);
            }
            else
            {
                uint32_t& depth = t_depth[index];
                ++depth;
                Frame          frame{index, lists, args, result, original};
                wxl_point_call call{sizeof(wxl_point_call), index + 1, WXL_POINT_BEFORE, depth, 0u,
                                    kRows[index].arg_count, args, result, {0, 0, 0}};
                RunSide(lists->side[WXL_POINT_BEFORE], call);
                RunReplace(frame, 0, &call);
                call.side = WXL_POINT_AFTER;
                RunSide(lists->side[WXL_POINT_AFTER], call);
                --depth;
            }
        }
        Settle();
    }

    void EnterInlined(uint32_t index, void* const* args)
    {
        if (index >= kCount || !g_slots[index].lists.load(std::memory_order_relaxed)) return;
        {
            const InFlight counted;
            if (const Lists* lists = g_slots[index].lists.load(std::memory_order_seq_cst))
            {
                wxl_point_call call{sizeof(wxl_point_call), index + 1, WXL_POINT_BEFORE, t_depth[index] + 1,
                                    WXL_POINT_CALL_INLINED, kRows[index].arg_count, args, nullptr, {0, 0, 0}};
                RunSide(lists->side[WXL_POINT_BEFORE], call);
            }
        }
        Settle();
    }

    void ReleaseOf(uint32_t mod)
    {
        std::lock_guard<std::mutex> lock(g_write);
        std::vector<Link*> owned;
        for (auto& [id, link] : g_links)
            if (link->owner == mod) owned.push_back(link.get());
        std::sort(owned.begin(), owned.end(), [](const Link* a, const Link* b) { return a->id < b->id; });
        for (Link* link : owned) Unlink(link);
        if (!owned.empty())
            WLOG_INFO(kTag, "detached %zu point handler(s) %s left attached", owned.size(), Who(mod).c_str());
    }

    void ReleaseAll()
    {
        std::lock_guard<std::mutex> lock(g_write);
        std::vector<Link*> all;
        for (auto& [id, link] : g_links) all.push_back(link.get());
        for (Link* link : all) Unlink(link);
        if (!all.empty()) WLOG_INFO(kTag, "detached %zu point handler(s) still attached on the way out", all.size());
    }

    void Describe()
    {
        uint32_t here = 0;
        for (uint32_t i = 0; i < kCount; ++i)
        {
            const char* why = "";
            if (Has(i, &why))
            {
                ++here;
                WLOG_DEBUG(kTag, "point %u %s %s: %s, %s", i + 1, kRows[i].name, kRows[i].lowered,
                           (kRows[i].flags & WXL_POINT_REPLACEABLE) ? "replace" : "observe",
                           kRows[i].thread == WXL_POINT_THREAD_MAIN ? "main thread" : "any thread");
            }
            else
                WLOG_DEBUG(kTag, "point %u %s: not on this runtime: %s", i + 1, kRows[i].name, why);
        }
        WLOG_INFO(kTag, "%u of %u point(s) on this runtime", here, static_cast<unsigned>(kCount));
    }
}
