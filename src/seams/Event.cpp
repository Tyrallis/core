// The Event seams: what the runtime does at the scheduler's tick, on both builds.
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

#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace wxl::seam::Event::DispatchUpdate
{
    void WXL_CALL Before(void*, uint32_t, float elapsed_sec)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_update_args args{};
        args.delta = elapsed_sec;
        host->Raise(WXL_EV_Update, &args);
    }

    void WXL_CALL After(void*, uint32_t, float)
    {
        // The tick's fact is that it started; the dispatch below this seam is the client's own
        // handlers, which a mod does not observe.
    }
}
