// The wxl.script service: the SDK's script table over the client's own Lua, on both builds.
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

        /// Withdraws every function a mod registered: the host's disable handler.
        void OnModDisabled(uint32_t token);

        bool     Live();
        uint32_t Context();
        void*    State();
    }
}
