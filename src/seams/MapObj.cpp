// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The MapObj seam: what the runtime does when a map object's root file is about to be walked, on
// both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace wxl::seam::MapObj::Load
{
    void WXL_CALL Before(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self || !host->Listening(WXL_EV_MapObjectLoad)) return;

        uint32_t    size = 0;
        const void* data = wxl::core::binding::map_obj_bytes(self, &size);
        if (!data) return;
        const char* path = wxl::core::binding::map_obj_path(self);
        if (!path) path = "";

        wxl_map_object_args args{};
        args.path = wxl_str{path, static_cast<uint32_t>(strnlen(path, 260))};
        args.data = data;
        args.size = size;
        host->Raise(WXL_EV_MapObjectLoad, &args);
    }
}

namespace wxl::seam::MapObj::CreateGroup
{
    void WXL_CALL Before(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self || !host->Listening(WXL_EV_MapObjectGroupLoad)) return;

        void*       parent = nullptr;
        uint32_t    index  = 0;
        uint32_t    size   = 0;
        const void* data   = wxl::core::binding::map_obj_group_bytes(self, &parent, &index, &size);
        if (!data) return;
        const char* root = parent ? wxl::core::binding::map_obj_path(parent) : nullptr;
        if (!root) root = "";

        wxl_map_object_group_args args{};
        args.path  = wxl_str{root, static_cast<uint32_t>(strnlen(root, 260))};
        args.group = index;
        args.data  = data;
        args.size  = size;
        host->Raise(WXL_EV_MapObjectGroupLoad, &args);
    }
}
