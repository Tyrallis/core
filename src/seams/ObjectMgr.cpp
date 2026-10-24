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

#include <cstdint>

namespace
{
    /// Tracks an object the client has built or brought back and announces it with its handle.
    void Enter(void* object)
    {
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
