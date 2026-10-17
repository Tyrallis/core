// The x64 attachment: nothing to detour, the accessors the client handed over, and the services.
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

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "seams/Table.hpp"

#include "wxl/services/object.h"

namespace wxl::core::attachment
{
    void Prepare() {}

    std::vector<wxl_event> Events()
    {
        // The seam bodies are the same code on both architectures, so the events they raise are
        // the same list: the one the seam table derives.
        return seams::RaisedEvents();
    }

    std::vector<wxl::host::PublishedService> Services()
    {
        return {
            { WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION, objects::Api() },
        };
    }

    void Install()
    {
        // Nothing to attach: the client's source calls Before and After itself. What is checked
        // is what it handed over, since a seam's body reads the client through it.
        for (const seams::Seam& seam : seams::Table())
            WLOG_INFO(kTag, "seam %s: Before and After are calls %s makes", seam.name, seam.symbol);
        for (const char* name : binding::Unbound())
            WLOG_WARN(kTag, "the client handed no %s accessor; what reads it answers absent", name);
    }
}
