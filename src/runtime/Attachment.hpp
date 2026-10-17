// What an architecture's attachment to the client supplies to the shared start-up.
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
}
