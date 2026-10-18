// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The FrameScript seams: what the runtime does when the client's script context comes and goes, on both builds.

#include "wxl/seams/seams.h"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Stages.hpp"
#include "services/ScriptService.hpp"

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"

#include <cstdint>

namespace
{
    /// The context the mods were last told is live, so that ScriptVmClosing names the one
    /// ScriptVmReady did whatever CGGameUI's flags say by then.
    bool     g_live    = false;
    uint32_t g_context = WXL_SCRIPT_CONTEXT_GLUE;

    /// Which interface the context being created serves: the game's while CGGameUI is logging
    /// in (InitializeFrameXML sets m_loggingIn before it creates the context) or initialised
    /// (ReloadUI recreates it with m_initialized still set), the glue's otherwise.
    uint32_t ContextNow()
    {
        return wxl::core::binding::ui_logging_in() || wxl::core::binding::ui_initialized()
                   ? WXL_SCRIPT_CONTEXT_GAME
                   : WXL_SCRIPT_CONTEXT_GLUE;
    }

    const char* ContextName(uint32_t context)
    {
        return context == WXL_SCRIPT_CONTEXT_GAME ? "game" : "glue";
    }
}

namespace wxl::seam::FrameScript::Initialize
{
    void WXL_CALL After(int32_t, int32_t)
    {
        void* state = wxl::core::binding::script_state();
        g_context   = ContextNow();
        g_live      = true;
        wxl::core::services::script::OnContextReady(g_context, state);
        WLOG_DEBUG(wxl::core::kTag, "script context up: %s", ContextName(g_context));

        // There is a script VM and an API can be registered on it, which is the stage exactly.
        // The client builds more than one in a session; only the first is news.
        wxl::core::stages::Reach(WXL_STAGE_SCRIPT);

        wxl::host::ModHost* host = wxl::core::Host();
        if (!host) return;
        wxl_script_vm_args args{};
        args.context      = g_context;
        args.native_state = state;
        host->Raise(WXL_EV_ScriptVmReady, &args);
    }
}

namespace wxl::seam::FrameScript::Destroy
{
    void WXL_CALL Before()
    {
        if (!g_live) return;
        WLOG_DEBUG(wxl::core::kTag, "script context closing: %s", ContextName(g_context));

        if (wxl::host::ModHost* host = wxl::core::Host())
        {
            wxl_script_vm_args args{};
            args.context      = g_context;
            args.native_state = wxl::core::binding::script_state();
            host->Raise(WXL_EV_ScriptVmClosing, &args);
        }
        wxl::core::services::script::OnContextClosing();
        g_live = false;
    }
}

namespace wxl::seam::FrameScript::CreateEvents
{
    void WXL_CALL After(const char** names, uint32_t count)
    {
        // The service installs the table again with its own events behind the client's; that
        // call reaches this seam too, and is not a new list.
        if (wxl::core::services::script::Reissuing()) return;
        wxl::core::services::script::OnClientEventsInstalled(names, count);
    }
}
