// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What a Direct3D 9 texture or buffer holds, asked of the resource itself: its pool and the bytes
// its format and dimensions take. Both realisations of the gx_resources binding row describe the
// device's resources with this, so the two clients count them the same way.
//
// Header-only C++, Windows only: it includes <d3d9.h>. Called on the thread that renders.

#pragma once

#include "wxl/core/init.h"

#include <d3d9.h>

#include <cstdint>

namespace wxl::core::gpu
{
    /// The bytes one surface of a format takes: a block format by its 4x4 blocks, the others by
    /// their bits per texel. A format this does not know is counted at 32 bits.
    inline uint64_t SurfaceBytes(D3DFORMAT format, uint64_t width, uint64_t height)
    {
        const uint64_t blocks = ((width + 3) / 4) * ((height + 3) / 4);
        switch (static_cast<uint32_t>(format))
        {
        case D3DFMT_DXT1: return blocks * 8;
        case D3DFMT_DXT2:
        case D3DFMT_DXT3:
        case D3DFMT_DXT4:
        case D3DFMT_DXT5: return blocks * 16;
        default: break;
        }
        uint64_t bits = 32;
        switch (format)
        {
        case D3DFMT_A8:
        case D3DFMT_L8:
        case D3DFMT_P8:
        case D3DFMT_R3G3B2:
        case D3DFMT_A4L4: bits = 8; break;
        case D3DFMT_R5G6B5:
        case D3DFMT_X1R5G5B5:
        case D3DFMT_A1R5G5B5:
        case D3DFMT_A4R4G4B4:
        case D3DFMT_X4R4G4B4:
        case D3DFMT_A8L8:
        case D3DFMT_L16:
        case D3DFMT_D16:
        case D3DFMT_D16_LOCKABLE:
        case D3DFMT_R16F:
        case D3DFMT_V8U8: bits = 16; break;
        case D3DFMT_R8G8B8: bits = 24; break;
        case D3DFMT_A16B16G16R16:
        case D3DFMT_A16B16G16R16F:
        case D3DFMT_G32R32F: bits = 64; break;
        case D3DFMT_A32B32G32R32F: bits = 128; break;
        default: break;
        }
        return width * height * bits / 8;
    }

    /**
     * @brief Describes a texture: its pool and every level of it, the six faces of a cube.
     * @return false for a resource that is not a texture.
     */
    inline bool DescribeTexture(IDirect3DBaseTexture9* texture, wxl_core_gpu_resource* out)
    {
        if (!texture || !out) return false;
        out->kind  = WXL_CORE_GPU_TEXTURE;
        out->pool  = 0;
        out->bytes = 0;
        const DWORD levels = texture->GetLevelCount();
        switch (texture->GetType())
        {
        case D3DRTYPE_TEXTURE:
            for (DWORD i = 0; i < levels; ++i)
            {
                D3DSURFACE_DESC desc{};
                if (FAILED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(i, &desc))) break;
                out->pool = static_cast<uint32_t>(desc.Pool);
                out->bytes += SurfaceBytes(desc.Format, desc.Width, desc.Height);
            }
            return true;
        case D3DRTYPE_CUBETEXTURE:
            for (DWORD i = 0; i < levels; ++i)
            {
                D3DSURFACE_DESC desc{};
                if (FAILED(static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(i, &desc))) break;
                out->pool = static_cast<uint32_t>(desc.Pool);
                out->bytes += 6 * SurfaceBytes(desc.Format, desc.Width, desc.Height);
            }
            return true;
        case D3DRTYPE_VOLUMETEXTURE:
            for (DWORD i = 0; i < levels; ++i)
            {
                D3DVOLUME_DESC desc{};
                if (FAILED(static_cast<IDirect3DVolumeTexture9*>(texture)->GetLevelDesc(i, &desc))) break;
                out->pool = static_cast<uint32_t>(desc.Pool);
                out->bytes += desc.Depth * SurfaceBytes(desc.Format, desc.Width, desc.Height);
            }
            return true;
        default: return false;
        }
    }

    /**
     * @brief Describes a vertex or an index buffer: its pool and its size.
     * @return false for a resource that is neither.
     */
    inline bool DescribeBuffer(IDirect3DResource9* buffer, wxl_core_gpu_resource* out)
    {
        if (!buffer || !out) return false;
        out->kind = WXL_CORE_GPU_BUFFER;
        switch (buffer->GetType())
        {
        case D3DRTYPE_VERTEXBUFFER:
        {
            D3DVERTEXBUFFER_DESC desc{};
            if (FAILED(static_cast<IDirect3DVertexBuffer9*>(buffer)->GetDesc(&desc))) return false;
            out->pool  = static_cast<uint32_t>(desc.Pool);
            out->bytes = desc.Size;
            return true;
        }
        case D3DRTYPE_INDEXBUFFER:
        {
            D3DINDEXBUFFER_DESC desc{};
            if (FAILED(static_cast<IDirect3DIndexBuffer9*>(buffer)->GetDesc(&desc))) return false;
            out->pool  = static_cast<uint32_t>(desc.Pool);
            out->bytes = desc.Size;
            return true;
        }
        default: return false;
        }
    }
} // namespace wxl::core::gpu
