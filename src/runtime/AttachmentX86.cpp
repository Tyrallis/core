// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The x86 attachment: the hook engine, the detours, and the services only a shipped binary has.

#include "runtime/Attachment.hpp"

#include "binding/BindingX86.hpp"
#include "common/Log.hpp"
#include "hook/Engine.hpp"
#include "objects/Objects.hpp"
#include "runtime/Core.hpp"
#include "runtime/Image.hpp"
#include "seams/AttachX86.hpp"
#include "seams/Table.hpp"
#include "services/HookService.hpp"
#include "services/PatchService.hpp"
#include "services/DataService.hpp"
#include "services/ScriptService.hpp"
#include "services/StorageService.hpp"

#include "wxl/services/hook.h"
#include "wxl/services/object.h"
#include "wxl/services/patch.h"
#include "wxl/services/data.h"
#include "wxl/services/script.h"
#include "wxl/services/storage.h"

namespace wxl::core::attachment
{
    void Prepare()
    {
        binding::x86::Prepare();
        if (!wxl::hook::Init())
            WLOG_ERROR(kTag, "hook engine unavailable; detours will be refused");
    }

    std::vector<wxl_event> Events()
    {
        // The seam bodies are the same code on both architectures, so the events they raise are
        // the same list: the one the seam table derives.
        return seams::RaisedEvents();
    }

    std::vector<wxl::host::PublishedService> Services()
    {
        return {
            { WXL_HOOK_SERVICE, WXL_HOOK_SERVICE_VERSION, &services::HookApi() },
            { WXL_HOOK_RAW_SERVICE, WXL_HOOK_RAW_SERVICE_VERSION, &services::HookRawApi() },
            { WXL_PATCH_SERVICE, WXL_PATCH_SERVICE_VERSION, &services::PatchApi() },
            { WXL_OBJECT_SERVICE, WXL_OBJECT_SERVICE_VERSION, objects::Api() },
            { WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, &services::ScriptApi() },
            { WXL_STORAGE_SERVICE, WXL_STORAGE_SERVICE_VERSION, &services::StorageApi() },
            { WXL_DATA_SERVICE, WXL_DATA_SERVICE_VERSION, &services::DataApi() },
        };
    }

    void Release(uint32_t mod) { services::ReleaseHooksOf(mod); }

    void ReleaseAll() { services::ReleaseEveryHook(); }

    void Install()
    {
        // The core's own detours go in before any mod can attach behind them, and only in the
        // image whose addresses they are.
        if (!image::IsClient())
            WLOG_WARN(kTag, "the executable is not Wow.exe build %u: named hook points and the "
                            "seams are unavailable; no world event will be raised and wxl.object "
                            "sees no object",
                      static_cast<unsigned>(image::kBuild));
        else if (!seams::Install())
            WLOG_ERROR(kTag, "a seam is not attached; its events will not be raised");
    }
}
