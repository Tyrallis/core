// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The BLPFile seams, with the texture read callback's: a texture file's header, as the client
// decodes it.
//
// CBLPFile::Source is handed a texture file's bytes by its two callers, CBLPFile::Open and the
// texture service's PumpBlpTextureAsync (called by AsyncTextureSuccessCallback), neither of
// which tells it the file's name. Each caller's seam notes the name on its thread first; Source's
// raises TextureDecode with it.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"
#include "services/Assets.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>
#include <cstring>

namespace
{
    /// The name of the texture file this thread is about to decode, as its caller gave it.
    thread_local char t_texture[260];

    void Note(const char* name)
    {
        const char* inner = nullptr;
        if (wxl::core::services::assets::IsLayerPath(name, nullptr, &inner)) name = inner;
        if (!name) name = "";
        const size_t length = strnlen(name, sizeof t_texture - 1);
        std::memcpy(t_texture, name, length);
        t_texture[length] = 0;
    }

    /// The fields of a BLP2 header the event reports: a file format, the same on every build.
    struct BlpHeader
    {
        uint32_t magic;
        uint32_t version;
        uint8_t  encoding;
        uint8_t  alpha_bits;
        uint8_t  preferred_format;
        uint8_t  has_mips;
        uint32_t width;
        uint32_t height;
        uint32_t mip_offsets[16];
        uint32_t mip_sizes[16];
    };
    static_assert(sizeof(BlpHeader) == 0x94);

    constexpr uint32_t kBlp2 = 0x32504C42u; // "BLP2"
}

namespace wxl::seam::Texture::AsyncTextureSuccessCallback
{
    void WXL_CALL Before(void* texture)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !host->Listening(WXL_EV_TextureDecode)) return;
        Note(texture ? wxl::core::binding::texture_name(texture) : "");
    }
}

namespace wxl::seam::BLPFile::Open
{
    void WXL_CALL Before(void*, const char* file_name, int32_t)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !host->Listening(WXL_EV_TextureDecode)) return;
        Note(file_name);
    }
}

namespace wxl::seam::BLPFile::Source
{
    void WXL_CALL Before(void*, const void* data)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !data || !host->Listening(WXL_EV_TextureDecode)) return;

        BlpHeader header;
        std::memcpy(&header, data, sizeof header);
        if (header.magic != kBlp2) return;
        uint32_t levels = 0;
        while (levels < 16 && header.mip_sizes[levels]) ++levels;

        wxl_texture_decode_args args{};
        args.path       = wxl_str{t_texture, static_cast<uint32_t>(std::strlen(t_texture))};
        args.width      = header.width;
        args.height     = header.height;
        args.encoding   = header.encoding;
        args.alpha_bits = header.alpha_bits;
        args.alpha_kind = header.preferred_format;
        args.mip_levels = levels;
        host->Raise(WXL_EV_TextureDecode, &args);
        t_texture[0] = 0;
    }
}
