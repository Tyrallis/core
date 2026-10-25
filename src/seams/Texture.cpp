// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The Texture seam: what the runtime does when the client asks for a texture by name, on both
// builds.

#include "wxl/seams/seams.h"

#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace wxl::seam::Texture::Create
{
    void WXL_CALL Before(const char* file_name, uint32_t, void*, uint32_t)
    {
        // Every texture reference of the client comes through here: nothing is read until
        // someone listens.
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !host->Listening(WXL_EV_TextureLoad) || !file_name || !*file_name) return;

        wxl_texture_load_args args{};
        args.path = wxl_str{file_name, static_cast<uint32_t>(std::strlen(file_name))};
        host->Raise(WXL_EV_TextureLoad, &args);
    }
}
