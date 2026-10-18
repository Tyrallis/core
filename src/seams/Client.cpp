// The Client seam: what the runtime does as the client takes itself apart.
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

#include "common/Log.hpp"
#include "runtime/Attachment.hpp"
#include "runtime/Core.hpp"
#include "runtime/Stages.hpp"

#include "wxl/host/mod_host.hpp"

namespace wxl::seam::Client::Destroy
{
    void WXL_CALL Before()
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        // The last thing a mod is told, and the only chance it has to put something away. It
        // runs here rather than at DLL_PROCESS_DETACH because by then the loader lock is held,
        // other libraries may already be gone, and nothing a mod did would be safe.
        wxl::core::stages::Reach(WXL_STAGE_SHUTDOWN);

        // Whatever a mod did not detach itself. This has to happen between the stage, where a
        // mod may still want its hooks, and the unload, which frees the code they jump to.
        wxl::core::attachment::ReleaseAll();

        host->UnloadAll();
        WLOG_INFO(wxl::core::kTag, "mods unloaded; the client is on its own from here");
    }

    void WXL_CALL After()
    {
        // Nothing: no mod's code is left to run, and the host is empty.
    }
}
