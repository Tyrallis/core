// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The numbers of the stock client's draw of one model batch, rebuilt after CM2SceneRender::DrawBatch
// (0x008203B0) returns: the stock client keeps no record of them, but everything they were made
// from is still there. Header-only, so the seam harness drives it over stand-ins of its structs.

#pragma once

#include "wxl/events.h"
#include "wxl/offsets/layout.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wxl::core::binding::x86::model_draw
{
    constexpr uint32_t kApiD3d9         = 1;    // EGxApi
    constexpr uint32_t kApiD3d9Ex       = 2;
    constexpr uint32_t kGxPrimTriangles = 3;    // EGxPrim: the only primitive DrawBatch draws
    constexpr uint32_t kSharedVertices  = 0x40; // CM2Cache::m_flags: a one-bone model shares its vertices

    /// What the rebuild reads outside the renderer: the client's statics.
    struct Globals
    {
        const void*     device;     ///< g_theGxDevicePtr
        int32_t         shaders;    ///< CShaderEffect::s_enableShaders
        const uint32_t* d3d9;       ///< CGxDeviceD3d::s_primitiveConversion
        const uint32_t* d3d9ex;     ///< CGxDeviceD3d9Ex::s_primitiveConversion
    };

    /// A value of T at a byte offset from a base, read without alignment assumptions.
    template <class T>
    T Read(const void* base, std::size_t offset)
    {
        T value;
        std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof value);
        return value;
    }

    /// The model at the top of what `model` hangs on.
    inline void* RootModel(void* model)
    {
        // Bounded: an attachment chain is a few links (weapon, rider, mount).
        for (int i = 0; model && i < 16; ++i)
        {
            void* parent = Read<void*>(model, WXL_FIELD("CM2Model", "m_attachParent"));
            if (!parent) break;
            model = parent;
        }
        return model;
    }

    /// The viewport the device last set, as IXformSetViewport (0x006A99E0) computes it from the
    /// device's viewport and window, which it last did in the draw's state sync.
    inline void Viewport(const void* device, wxl_model_batch_args* out)
    {
        const float maxX = Read<float>(device, WXL_FIELD("CGxDevice", "m_curWindowRect.maxX"));
        const float maxY = Read<float>(device, WXL_FIELD("CGxDevice", "m_curWindowRect.maxY"));
        const float xl   = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_x.m_l"));
        const float xh   = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_x.m_h"));
        const float yl   = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_y.m_l"));
        const float yh   = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_y.m_h"));
        out->viewport_x      = static_cast<uint32_t>(xl * maxX + 0.5f);
        out->viewport_y      = static_cast<uint32_t>((1.0f - yh) * maxY + 0.5f);
        out->viewport_width  = static_cast<uint32_t>(xh * maxX - static_cast<float>(out->viewport_x) + 0.5f);
        out->viewport_height = static_cast<uint32_t>((1.0f - yl) * maxY - static_cast<float>(out->viewport_y) + 0.5f);
        out->viewport_min_z  = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_z.m_l"));
        out->viewport_max_z  = Read<float>(device, WXL_FIELD("CGxDevice", "m_viewport.m_z.m_h"));
        // Drawing into a texture, Y counts from the other edge.
        if (Read<void*>(device, WXL_FIELD("CGxDevice", "m_textureTarget[0].m_apiSpecific")) ||
            Read<void*>(device, WXL_FIELD("CGxDevice", "m_textureTarget[1].m_apiSpecific")))
            out->viewport_y = static_cast<uint32_t>(maxY * yl);
    }

    /**
     * @brief The draw of the batch `render` (a CM2SceneRender) has just issued, as
     *        DrawIndexedPrimitive was handed it; false when the device drew nothing.
     *
     * DrawBatch builds a CGxBatch from the current skin section and hands it to the device's Draw
     * (CGxDeviceD3d 0x006A3620, CGxDeviceD3d9Ex 0x006A77C0), which adds the offsets of the bound
     * vertex and index buffers. The same steps are taken here from the section, which the renderer
     * still points at, and the device's state, which nothing has changed since.
     */
    inline bool Rebuild(const void* render, const Globals& globals, wxl_model_batch_args* out)
    {
        const void* device = globals.device;
        if (!render || !device || !out) return false;
        const uint32_t api = Read<uint32_t>(device, WXL_FIELD("CGxDevice", "m_api"));
        if (api != kApiD3d9 && api != kApiD3d9Ex) return false;
        // Draw issues nothing without a context, nor while the device is lost.
        if (!Read<uint32_t>(device, WXL_FIELD("CGxDevice", "m_context")) || Read<uint32_t>(device, WXL_FIELD("CGxDevice", "m_intF5C")))
            return false;
        void*       model    = Read<void*>(render, WXL_FIELD("CM2SceneRender", "m_curModel"));
        const void* section  = Read<const void*>(render, WXL_FIELD("CM2SceneRender", "m_curSkinSection"));
        const void* vertices = Read<const void*>(device, WXL_FIELD("CGxDevice", "m_primVertexFormatBuf"));
        const void* indices  = Read<const void*>(device, WXL_FIELD("CGxDevice", "m_primIndexBuf"));
        if (!model || !section || !indices) return false;

        // The batch: a triangle list over the section's indices and vertices. Without shaders, a
        // model skinned on the CPU has its section's vertices at the start of the stream.
        const uint16_t vertexStart = Read<uint16_t>(section, WXL_FIELD("M2SkinSection", "m_vertexStart"));
        const uint16_t vertexCount = Read<uint16_t>(section, WXL_FIELD("M2SkinSection", "m_vertexCount"));
        const uint16_t indexStart  = Read<uint16_t>(section, WXL_FIELD("M2SkinSection", "m_indexStart"));
        const uint16_t indexCount  = Read<uint16_t>(section, WXL_FIELD("M2SkinSection", "m_indexCount"));
        uint16_t       minIndex    = vertexStart;
        if (!globals.shaders)
        {
            const void* file   = Read<const void*>(model, WXL_FIELD("CM2Model", "m_shared"));
            const void* header = file ? Read<const void*>(file, WXL_FIELD("CM2Shared", "m_data")) : nullptr;
            const void* cache  = Read<const void*>(render, WXL_FIELD("CM2SceneRender", "m_cache"));
            const bool  shared = header && cache && Read<uint32_t>(header, WXL_FIELD("M2Data", "m_bones.m_count")) == 1 &&
                                (Read<uint32_t>(cache, WXL_FIELD("CM2Cache", "m_flags")) & kSharedVertices);
            if (!shared) minIndex = 0;
        }
        // CGxBatch holds both ends in 16 bits.
        const uint16_t maxIndex = static_cast<uint16_t>(minIndex + vertexCount - 1);

        // The device's draw: the vertex stream's offset as a base vertex when the device cannot
        // offset a stream, the index buffer's offset added to the batch's start.
        uint32_t base = 0;
        if (!Read<uint32_t>(device, WXL_FIELD("CGxDevice", "m_caps.m_streamOffset")) && vertices)
        {
            const uint32_t stride = Read<uint32_t>(vertices, WXL_FIELD("CGxBuf", "m_itemSize"));
            base = stride ? Read<uint32_t>(vertices, WXL_FIELD("CGxBuf", "m_index")) / stride : 0u;
        }
        const uint32_t* conversion = api == kApiD3d9 ? globals.d3d9 : globals.d3d9ex;

        out->model           = model;
        out->root_model      = RootModel(model);
        out->primitive_type  = conversion ? conversion[kGxPrimTriangles] : 0u;
        out->base_vertex     = static_cast<int32_t>(base);
        out->min_index       = minIndex;
        out->vertex_count    = static_cast<uint32_t>(maxIndex) - minIndex + 1;
        out->start_index     = indexStart + (Read<uint32_t>(indices, WXL_FIELD("CGxBuf", "m_index")) >> 1);
        out->primitive_count = indexCount / 3u; // CGxDevice::PrimCalcCount of a triangle list
        const void* material = Read<const void*>(render, WXL_FIELD("CM2SceneRender", "m_curMaterial"));
        out->blend_mode      = material ? Read<uint16_t>(material, WXL_FIELD("M2Material", "m_blendMode")) : 0u;
        Viewport(device, out);
        return true;
    }
}
