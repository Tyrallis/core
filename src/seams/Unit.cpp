// The Unit seams: what the runtime does at a unit's death and aura changes, on both builds.
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
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// Raises UnitAuraChanged for a tracked unit; an untracked one is logged and skipped.
    void AuraChanged(void* self, int32_t spellId, wxl_bool removed)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_unit_aura_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE)
        {
            WLOG_DEBUG(wxl::core::kTag, "aura change on an untracked unit; no event");
            return;
        }
        args.spell_id = static_cast<uint32_t>(spellId);
        args.removed  = removed;
        host->Raise(WXL_EV_UnitAuraChanged, &args);
    }
}

namespace wxl::seam::Unit::OnDeath
{
    void WXL_CALL Before(void*) {}

    void WXL_CALL After(void* self)
    {
        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;

        wxl_unit_args args{};
        args.unit = wxl::core::objects::HandleOf(self);
        if (args.unit == WXL_HANDLE_NONE)
        {
            WLOG_DEBUG(wxl::core::kTag, "death of an untracked unit; no event");
            return;
        }
        host->Raise(WXL_EV_UnitDeath, &args);
    }
}

namespace wxl::seam::Unit::OnAuraAdded
{
    void WXL_CALL Before(void*, uint32_t, uint8_t, void*, int32_t) {}

    void WXL_CALL After(void* self, uint32_t, uint8_t, void*, int32_t spell_id)
    {
        AuraChanged(self, spell_id, WXL_FALSE);
    }
}

namespace wxl::seam::Unit::OnAuraRemoved
{
    void WXL_CALL Before(void*, uint32_t, uint8_t, void*, int32_t) {}

    void WXL_CALL After(void* self, uint32_t, uint8_t, void*, int32_t spell_id)
    {
        AuraChanged(self, spell_id, WXL_TRUE);
    }
}
