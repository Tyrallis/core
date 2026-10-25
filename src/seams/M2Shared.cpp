// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The M2Shared seams: a model file read (ModelLoad), parsed (ModelParsed), its skin profile read and
// parsed (SkinProfileLoad, SkinProfileParsed) and an external sequence read (ModelSequenceLoad).

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    void RaiseModelLoad(void* param)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !param || !host->Listening(WXL_EV_ModelLoad)) return;

        uint32_t    size = 0;
        const void* data = wxl::core::binding::model_bytes(param, &size);
        if (!data) return;
        const char* path = wxl::core::binding::model_path(param);
        if (!path) path = "";

        const void* replacement      = nullptr;
        uint32_t    replacement_size = 0;

        wxl_model_load_args args{};
        args.path             = wxl_str{path, static_cast<uint32_t>(strnlen(path, 260))};
        args.data             = data;
        args.size             = size;
        args.replacement      = &replacement;
        args.replacement_size = &replacement_size;
        host->Raise(WXL_EV_ModelLoad, &args);

        // The last handler to write wins. Its bytes are copied into the client's own buffer,
        // so they only need to outlive the raise.
        if (!replacement || !replacement_size) return;
        if (wxl::core::binding::model_replace_bytes(param, replacement, replacement_size))
            WLOG_DEBUG(wxl::core::kTag, "model %s replaced by a mod: %u bytes instead of %u", path,
                       static_cast<unsigned>(replacement_size), static_cast<unsigned>(size));
        else
            WLOG_WARN(wxl::core::kTag, "model %s: the %u bytes a mod answered could not be put in "
                                       "place; the client parses the file as read",
                      path, static_cast<unsigned>(replacement_size));
    }
}

namespace wxl::seam::M2Shared::LoadSucceededCallback
{
    void WXL_CALL Before(void* param)
    {
        RaiseModelLoad(param);
    }
}

namespace wxl::seam::M2Shared::FinishLoadingSkinProfile
{
    void WXL_CALL Before(void* self, uint32_t size)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self || !host->Listening(WXL_EV_SkinProfileLoad)) return;

        const void* data = wxl::core::binding::model_skin_bytes(self);
        if (!data) return;
        const char* model = wxl::core::binding::model_path(self);
        if (!model) model = "";

        wxl_skin_profile_args args{};
        args.model = wxl_str{model, static_cast<uint32_t>(strnlen(model, 260))};
        args.data  = data;
        args.size  = size;
        host->Raise(WXL_EV_SkinProfileLoad, &args);
    }
}

namespace
{
    /// The model file as the loader holds it, and its path as the events carry it; false when the
    /// client has none to show.
    bool Describe(void* self, wxl_core_m2_file& file, wxl_str& path)
    {
        file = wxl_core_m2_file{};
        if (!self || !wxl::core::binding::m2_file_info(self, &file)) return false;
        const char* name = file.path ? file.path : "";
        path             = wxl_str{name, static_cast<uint32_t>(strnlen(name, 260))};
        return true;
    }

    wxl::host::ModHost* Listener(wxl_event event)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        return host && host->Listening(event) ? host : nullptr;
    }
}

namespace wxl::seam::M2Shared::Initialize
{
    void WXL_CALL Before(void* self)
    {
        wxl::host::ModHost* host = Listener(WXL_EV_ModelParsed);
        wxl_core_m2_file    file;
        wxl_model_parsed_args args{};
        if (!host || !Describe(self, file, args.path)) return;
        args.file   = self;
        args.header = file.header;
        args.size   = file.size;
        host->Raise(WXL_EV_ModelParsed, &args);
    }
}

namespace wxl::seam::M2Shared::InitializeSkinProfile
{
    void WXL_CALL Before(void* self)
    {
        wxl::host::ModHost*          host = Listener(WXL_EV_SkinProfileParsed);
        wxl_core_m2_file             file;
        wxl_skin_profile_parsed_args args{};
        if (!host || !Describe(self, file, args.path) || !file.skin) return;
        args.file   = self;
        args.header = file.header;
        args.skin   = file.skin;
        host->Raise(WXL_EV_SkinProfileParsed, &args);
    }
}

namespace wxl::seam::M2Shared::FinishLoadingLowPrioritySequence
{
    void WXL_CALL Before(void* self, uint32_t sequence_index, uint8_t* buffer, uint32_t size)
    {
        wxl::host::ModHost*     host = Listener(WXL_EV_ModelSequenceLoad);
        wxl_core_m2_file        file;
        wxl_model_sequence_args args{};
        if (!host || !buffer || !Describe(self, file, args.path)) return;
        args.file           = self;
        args.sequence_index = sequence_index;
        args.data           = buffer;
        args.size           = size;
        host->Raise(WXL_EV_ModelSequenceLoad, &args);
    }
}
