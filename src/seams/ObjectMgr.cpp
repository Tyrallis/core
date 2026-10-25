// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The ObjectMgr seams: what the runtime does where objects enter and leave the client, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/objects/fields.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace
{
    // --- the watched fields ------------------------------------------------------------------
    //
    // The elements some mod's watch covers, as wxl.object's service asked for them, until the
    // last watch of one is given back. The client drops the handlers set on a kind's fields as
    // it leaves the world (DestroyShared) and the ones on the active player when that object
    // goes, so they are set again: on the kinds when the next world's InitializeShared has run,
    // on the active player when the client builds it or brings it back. Main thread only, like
    // the object manager.

    struct Element
    {
        wxl_object_type kind;   ///< the kind whose fields hold it
        uint32_t        first;  ///< its first dword
        uint32_t        dwords;
        bool            local;  ///< on the active player only
    };

    std::vector<Element> g_elements;
    bool                 g_sharedUp     = false;   ///< the client keeps its handlers on the kinds
    const void*          g_localObject  = nullptr; ///< the active player the local ones are on
    bool                 g_anyLocal     = false;

    /// Whether a mod receives a change: its own watch covers the element on that object.
    bool WatchesIt(void* user, uint32_t mod)
    {
        return wxl::core::objects::Service().Delivers(mod, *static_cast<const wxl_object_field_args*>(user));
    }

    /// The client's report of a change: the event, to the mods whose watch covers it.
    int32_t WXL_CALL OnFieldChanged(uint64_t guid, uint32_t offset, uint32_t size, const void* old_value,
                                    void* param)
    {
        const auto               kind  = static_cast<wxl_object_type>(reinterpret_cast<uintptr_t>(param));
        const wxl::field::Kind* owner = wxl::field::KindOf(kind);
        wxl::host::ModHost*      host  = wxl::core::Host();
        if (!owner || !host || (offset & 3u) || (size & 3u)) return 0;
        wxl_object_field_args args{};
        if (wxl::core::objects::Service().FieldChange(guid, kind, owner->first + offset / 4u, size / 4u,
                                                      old_value, args))
            host->RaiseTo(WXL_EV_ObjectFieldChanged, &args, &WatchesIt, &args);
        return 0;
    }

    void* ParamOf(const Element& element)
    {
        return reinterpret_cast<void*>(static_cast<uintptr_t>(element.kind));
    }

    uint32_t OffsetOf(const Element& element)
    {
        return (element.first - wxl::field::KindOf(element.kind)->first) * 4u;
    }

    bool SetOnKind(const Element& element)
    {
        return wxl::core::binding::object_watch_type(static_cast<uint32_t>(element.kind), OffsetOf(element),
                                                     element.dwords * 4u, &OnFieldChanged,
                                                     ParamOf(element)) == WXL_TRUE;
    }

    bool SetOnPlayer(const Element& element, const void* player)
    {
        return wxl::core::binding::object_watch_one(wxl::core::binding::object_guid(player),
                                                    static_cast<uint32_t>(element.kind), OffsetOf(element),
                                                    element.dwords * 4u, &OnFieldChanged,
                                                    ParamOf(element)) == WXL_TRUE;
    }

    /// The local elements onto the active player, when it is the object the client just built
    /// or brought back and they are not on it yet.
    void LocalPlayerSeen(const void* object)
    {
        if (!g_anyLocal || object == g_localObject) return;
        if (object != wxl::core::binding::local_player_object()) return;
        g_localObject = object;
        for (const Element& element : g_elements)
            if (element.local) SetOnPlayer(element, object);
    }

    void ObjectLeaving(const void* object)
    {
        if (object == g_localObject) g_localObject = nullptr;
    }

    /// Tracks an object the client has built or brought back and announces it with its handle.
    void Enter(void* object)
    {
        LocalPlayerSeen(object);
        wxl_object_args args{};
        args.object = wxl::core::objects::Track(object);
        args.guid   = wxl::core::binding::object_guid(object);
        args.type   = wxl::core::binding::object_type_id(object);

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        host->Raise(WXL_EV_ObjectCreate, &args);
    }

    /// Announces an object still tracked and forgets it; nothing for one already gone.
    void Leave(void* object)
    {
        if (!object) return;
        ObjectLeaving(object);

        // One lookup. Whether it is tracked, which handle it has, and what to retire are the
        // same question, and this runs for every object the client lets go.
        const wxl_handle handle = wxl::core::objects::HandleOf(object);
        if (handle == WXL_HANDLE_NONE) return;

        if (wxl::host::ModHost* host = wxl::core::Host())
        {
            wxl_object_args args{};
            args.object = handle;
            args.guid   = wxl::core::binding::object_guid(object);
            args.type   = wxl::core::binding::object_type_id(object);
            host->Raise(WXL_EV_ObjectDestroy, &args);
        }
        wxl::core::objects::Untrack(handle);
    }
}

namespace wxl::core::objects
{
    bool WatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local)
    {
        const wxl::field::Kind* owner = wxl::field::KindOf(kind);
        if (!owner || first < owner->first || dwords == 0 || first + dwords > owner->end) return false;
        const Element element{kind, first, dwords, local};
        g_elements.push_back(element);
        if (!local)
        {
            if (g_sharedUp) SetOnKind(element);
            return true;
        }
        g_anyLocal = true;
        if (g_localObject) SetOnPlayer(element, g_localObject);
        else LocalPlayerSeen(binding::local_player_object());
        return true;
    }

    bool UnwatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local)
    {
        const auto known = std::find_if(g_elements.begin(), g_elements.end(), [&](const Element& e) {
            return e.kind == kind && e.first == first && e.dwords == dwords && e.local == local;
        });
        if (known == g_elements.end()) return false;
        const Element element = *known;
        g_elements.erase(known);
        g_anyLocal = std::any_of(g_elements.begin(), g_elements.end(), [](const Element& e) { return e.local; });

        // Out of the world the client holds none of them: DestroyShared dropped the kinds', the
        // active player took its own with it. In it, the client unlinks the handler, or marks
        // it to go after the call when it is the one reporting right now.
        if (!local)
        {
            if (g_sharedUp)
                binding::object_unwatch_type(static_cast<uint32_t>(kind), OffsetOf(element), &OnFieldChanged,
                                             ParamOf(element));
        }
        else if (g_localObject)
        {
            binding::object_unwatch_one(binding::object_guid(g_localObject), static_cast<uint32_t>(kind),
                                        OffsetOf(element), &OnFieldChanged, ParamOf(element));
        }
        return true;
    }
}

namespace wxl::seam::ObjectMgr::InitializeShared
{
    void WXL_CALL After()
    {
        g_sharedUp = true;
        for (const Element& element : g_elements)
            if (!element.local) SetOnKind(element);
    }
}

namespace wxl::seam::ObjectMgr::DestroyShared
{
    void WXL_CALL Before()
    {
        g_sharedUp    = false;
        g_localObject = nullptr;
    }
}

namespace wxl::seam::ObjectMgr::InitObject
{
    void WXL_CALL After(void* object, const void*, uint32_t)
    {
        if (object) Enter(object);
    }
}

namespace wxl::seam::ObjectMgr::GetUpdateObject
{
    void WXL_CALL After(void* object, uint64_t, int32_t*)
    {
        // Every update of every object passes here, so the common case -- tracked -- is one
        // lookup. An untracked object is one ObjDelete let go and the update brought back:
        // `*reenabled` says so too, but not for one that was still on the reenabled list, and
        // tracking whatever the client hands back untracked covers both.
        if (!object || wxl::core::objects::IsTracked(object)) return;
        Enter(object);
    }
}

namespace wxl::seam::ObjectMgr::ObjDelete
{
    void WXL_CALL Before(void* object) { Leave(object); }
}

namespace wxl::seam::ObjectMgr::DestroyRemovedObject
{
    void WXL_CALL Before(void* object) { Leave(object); }
}
