// The x64 attachment: calls the client's own source makes. No detour, no address.
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

#include "runtime/Attachment.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"

// The seam table is not built yet: nothing is attached, so no event is raised and no service is
// published. wxl.hook.raw and wxl.patch never exist here; wxl.hook arrives with the seam table.

namespace wxl::core::attachment
{
    void Prepare() {}

    std::vector<wxl_event> Events()
    {
        return {};
    }

    std::vector<wxl::host::PublishedService> Services()
    {
        return {};
    }

    void Install()
    {
        WLOG_INFO(kTag, "no seams in this build: no event is raised and wxl.hook is not published");
    }
}
