// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The MapArea seams: what the runtime does when a terrain chunk is built, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::MapArea::PrepareChunk
{
    void WXL_CALL Before(void* self, int32_t chunk_x, int32_t chunk_y)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self || !host->Listening(WXL_EV_TerrainChunkBuild)) return;

        wxl_terrain_chunk_args args{};
        args.map_id  = static_cast<uint32_t>(wxl::core::binding::map_id());
        args.tile_x  = wxl::core::binding::map_area_tile_x(self);
        args.tile_y  = wxl::core::binding::map_area_tile_y(self);
        args.chunk_x = chunk_x;
        args.chunk_y = chunk_y;
        host->Raise(WXL_EV_TerrainChunkBuild, &args);
    }
}
