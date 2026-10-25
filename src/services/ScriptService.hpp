// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.script service: the SDK's script table over the client's own Lua, on both builds.

#pragma once

#include "wxl/services/script.h"

#include <cstdint>

namespace wxl::core::services
{
    /// The table published under WXL_SCRIPT_SERVICE, version WXL_SCRIPT_SERVICE_VERSION.
    const wxl_script_api& ScriptApi();

    // What the FrameScript seams and the host drive on the service. Everything a mod registered
    // lives exactly as long as the context it was registered on: the client's Lua state is
    // closed with the context, which takes every global and every event listener with it.
    namespace script
    {
        /// A context exists: registrations are accepted on it until OnContextClosing.
        void OnContextReady(uint32_t context, void* state);

        /// The context is about to be closed. Forgets every registration made on it.
        void OnContextClosing();

        /// The client installed its own event table on the live context. The service installs
        /// it again with the events mods declared appended, so each keeps an index to signal.
        void OnClientEventsInstalled(const char** names, uint32_t count);

        /// True while the service itself is installing the event table.
        bool Reissuing();

        /// The name at an index of the installed table -- the client's events, then the ones
        /// mods declared behind them -- or null for an index outside it.
        const char* EventName(uint32_t index);

        /// Withdraws every function a mod registered: the host's disable handler.
        void OnModDisabled(uint32_t token);

        bool     Live();
        uint32_t Context();
        void*    State();
    }
}
