// The ObjectMgr seams: what the runtime does where objects enter and leave the client, on both builds.
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
    void WXL_CALL Before(void*, const void*, uint32_t) {}

    void WXL_CALL After(void* object, const void*, uint32_t)
    {
        if (!object) return;

        wxl_object_args args{};
        args.object = wxl::core::objects::Track(object);
        args.guid   = wxl::core::binding::object_guid(object);
        args.type   = wxl::core::binding::object_type_id(object);

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        host->Raise(WXL_EV_ObjectCreate, &args);
    }
}

namespace wxl::seam::ObjectMgr::ObjDelete
{
    void WXL_CALL Before(void* object) { Leave(object); }

    void WXL_CALL After(void*) {}
}

namespace wxl::seam::ObjectMgr::DestroyRemovedObject
{
    void WXL_CALL Before(void* object) { Leave(object); }

    void WXL_CALL After(void*) {}
}
