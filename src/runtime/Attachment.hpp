// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// What an architecture's attachment to the client supplies to the shared start-up.

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

    /**
     * @brief Takes back what a mod still holds of the attachment: the detours it installed.
     *
     * A detour is a jump into the mod's binary. Freeing that binary with the jump still there
     * turns the next call the client makes into a jump to nothing, so this runs before a mod's
     * code goes away -- when it is disabled, and on the way out. A mod that detached its own
     * hooks first loses nothing by it.
     *
     * @param mod  the host's token for the mod (wxl::host::ModHost::ModToken).
     */
    void Release(uint32_t mod);

    /// The same for every mod at once, on the way out.
    void ReleaseAll();
}
