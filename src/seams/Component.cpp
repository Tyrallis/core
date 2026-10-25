// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The Component seams: what a character appearance puts on and takes off (AppearanceChanged),
// and the models wxl.m2 hung on a character kept on the interface copies the client strips.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "runtime/Core.hpp"
#include "services/M2Service.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <vector>

namespace
{
    void Raise(const void* component, int32_t item_slot, wxl_appearance_change change)
    {
        // Nothing at all is asked of the client while no mod listens.
        wxl::host::ModHost* host = wxl::core::Host();
        if (!component || !host || !host->Listening(WXL_EV_AppearanceChanged)) return;

        wxl_appearance_args args{};
        const uint32_t      slot = item_slot < 0 ? UINT32_MAX : static_cast<uint32_t>(item_slot);
        if (!wxl::core::binding::component_wearer(component, slot, &args)) return;
        args.change = change;
        if (change == WXL_APPEARANCE_FREED)
        {
            args.item_slot  = 0;
            args.display_id = 0;
        }
        else if (args.display_id == 0)
        {
            return; // an empty slot: nothing was put on, nothing comes off
        }
        host->Raise(WXL_EV_AppearanceChanged, &args);
    }
}

namespace wxl::seam::Component::AddItem
{
    void WXL_CALL After(void* self, int32_t item_slot, const void*, int32_t)
    {
        Raise(self, item_slot, WXL_APPEARANCE_WORN);
    }
}

namespace wxl::seam::Component::RemoveItem
{
    void WXL_CALL Before(void* self, int32_t item_slot)
    {
        Raise(self, item_slot, WXL_APPEARANCE_REMOVED);
    }
}

namespace wxl::seam::Component::FreeComponent
{
    void WXL_CALL Before(void* component)
    {
        Raise(component, 0, WXL_APPEARANCE_FREED);
    }
}

namespace
{
    // What RemoveVisuals is about to strip that wxl.m2 put there: a model a mod made (it keeps its
    // own reference, so it survives the detach), or an interface copy of one (it does not: the
    // parent held its only reference, so it is copied again from the model it was made from).
    struct Kept
    {
        void*    model;
        bool     copy;
        uint32_t attachment;
        wxl_bool to_root;
    };

    std::vector<Kept> g_kept;
    void*             g_keptFrom = nullptr;

    /// The points CCharacterComponent::RemoveVisuals detaches: only what hangs there is stripped.
    bool Stripped(uint32_t attachment)
    {
        switch (attachment)
        {
            case 12: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23:
            case 24: case 25: case 29: case 34: case 35: case 38:
                return true;
            default:
                return false;
        }
    }
}

namespace wxl::seam::Component::RemoveVisuals
{
    void WXL_CALL Before(void* model)
    {
        namespace binding = wxl::core::binding;
        namespace m2      = wxl::core::services::m2;
        g_kept.clear();
        g_keptFrom = nullptr;
        if (!model || !m2::HasModels()) return;

        void*          children[64];
        const uint32_t count = binding::m2_children(model, children, 64);
        for (uint32_t i = 0; i < count && i < 64; ++i)
        {
            void* source = m2::SourceOf(children[i]);
            if (!source) continue;
            Kept kept{source, source != children[i], 0, WXL_FALSE};
            if (binding::m2_attach_info(children[i], &kept.attachment, &kept.to_root) && Stripped(kept.attachment))
                g_kept.push_back(kept);
        }
        if (!g_kept.empty()) g_keptFrom = model;
    }

    void WXL_CALL After(void* model)
    {
        namespace binding = wxl::core::binding;
        if (!model || model != g_keptFrom) return;
        for (const Kept& kept : g_kept)
        {
            if (kept.copy)
                binding::m2_attach_copy(kept.model, model, kept.attachment, kept.to_root);
            else if (!binding::m2_model_parent(kept.model)) // stripped: hang it back
                binding::m2_attach_model(kept.model, model, kept.attachment, kept.to_root);
        }
        g_kept.clear();
        g_keptFrom = nullptr;
    }
}
