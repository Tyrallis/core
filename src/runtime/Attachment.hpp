// The attachment: how this build reaches the client, and what it can offer mods as a result.
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

#pragma once

#include "wxl/events.h"
#include "wxl/host/registry.hpp"

#include <cstdint>
#include <vector>

/// One implementation per architecture, chosen by the build: AttachmentX86.cpp detours the
/// shipped binary at resolved addresses and publishes the services built on that; AttachmentX64.cpp
/// is called by the client's own source and publishes what the seam table can serve. Core.cpp
/// calls these in order around the host's construction and never knows which one it got. The
/// seam bodies under src/seams/ read the client through the accessors at the end, which each
/// side fills its own way.
namespace wxl::core::attachment
{
    /**
     * @brief Readies whatever the attachment needs before the host exists.
     *
     * A failure is logged and the host still comes up: a mod that then asks for a service built
     * on the failed part is refused by that service, not by the runtime.
     */
    void Prepare();

    /// The events this build raises. A mod's subscribe to any other gets WXL_SUBSCRIPTION_NONE.
    std::vector<wxl_event> Events();

    /// The services this build publishes, announced before any mod loads.
    std::vector<wxl::host::PublishedService> Services();

    /**
     * @brief Attaches the runtime's own seams, once the host exists and before any mod loads,
     *        so that no mod can get in front of them.
     */
    void Install();

    /**
     * @brief Numeric id of the loaded map, -1 while none; the client's s_mapId (the Map.cpp one).
     *
     * On x86 a read of the static at its resolved address, -1 outside the client image; on x64
     * the accessor the client handed WarcraftXLInit, -1 when it handed none.
     */
    int32_t MapId();
}
