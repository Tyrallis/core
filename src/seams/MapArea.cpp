// The MapArea seams: what the runtime does when a terrain chunk is built, on both builds.
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
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::MapArea::PrepareChunk
{
    void WXL_CALL Before(void* self, int32_t chunk_x, int32_t chunk_y)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host || !self) return;

        wxl_terrain_chunk_args args{};
        args.map_id  = static_cast<uint32_t>(wxl::core::binding::map_id());
        args.tile_x  = wxl::core::binding::map_area_tile_x(self);
        args.tile_y  = wxl::core::binding::map_area_tile_y(self);
        args.chunk_x = chunk_x;
        args.chunk_y = chunk_y;
        host->Raise(WXL_EV_TerrainChunkBuild, &args);
    }

    void WXL_CALL After(void*, void*, int32_t, int32_t) {}
}
