// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The seam harness: the seam bodies, driven by hand against a real mod host and a fake client.

#include "binding/Binding.hpp"
#include "objects/Objects.hpp"
#include "runtime/Attachment.hpp"
#include "runtime/Frame.hpp"
#include "runtime/Stages.hpp"
#include "seams/Table.hpp"
#include "services/DataService.hpp"
#include "services/ScriptService.hpp"
#include "services/StorageService.hpp"

#include <chrono>

#include "wxl/events.h"
#include "wxl/host/mod_host.hpp"
#include "wxl/seams/seams.h"
#include "wxl/services/script.h"
#include "wxl/services/storage.h"

#if defined(_M_IX86)
    #include "seams/Detour.hpp"
    #include "seams/RegparmX86.hpp"
#endif

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

// --- What Core.cpp and the attachments supply in the DLL -------------------------------------

namespace
{
    std::unique_ptr<wxl::host::ModHost> g_host;
}

namespace wxl::core
{
    wxl::host::ModHost* Host() { return g_host.get(); }
}

namespace
{
    int g_released = 0;
}

// The attachment is per-architecture and the harness is neither, so it stands in for it. What
// the seam bodies ask of it is counted, which is how the checks below know it was asked.
namespace wxl::core::attachment
{
    void Release(uint32_t) { ++g_released; }
    void ReleaseAll() { ++g_released; }
}

// --- A fake client, handed to the binding as a wxl_core_init ---------------------------------

namespace
{
    /// An object as the fake client keeps it; the binding's accessors read these.
    struct FakeObject
    {
        uint64_t guid   = 0;
        uint32_t typeId = 0;
        uint32_t entry  = 0;
        wxl_vec3 position{};
        uint32_t health = 0;
        int32_t  visibleItems[19]{};
    };

    /// A visible item as CGPlayer_C::UpdateInventoryComponent is handed one.
    struct FakeVisibleItem
    {
        int32_t itemId = 0;
    };

    struct FakeArea
    {
        int32_t tileX = 0;
        int32_t tileY = 0;
    };

    int32_t     g_mapId       = -1;
    uint64_t    g_targetGuid  = 0;
    FakeObject* g_localPlayer = nullptr;

    const FakeObject* Obj(const void* object) { return static_cast<const FakeObject*>(object); }

    int32_t WXL_CALL FakeMapId(void) { return g_mapId; }
    const void* WXL_CALL FakeLocalPlayer(void) { return g_localPlayer; }
    uint64_t WXL_CALL FakeTargetGuid(void) { return g_targetGuid; }

    uint64_t WXL_CALL FakeGuid(const void* object) { return Obj(object)->guid; }
    uint32_t WXL_CALL FakeTypeId(const void* object) { return Obj(object)->typeId; }
    uint32_t WXL_CALL FakeEntry(const void* object) { return Obj(object)->entry; }
    uint32_t WXL_CALL FakeHealth(const void* object) { return Obj(object)->health; }

    wxl_bool WXL_CALL FakePosition(const void* object, wxl_vec3* out)
    {
        if (Obj(object)->typeId == WXL_OBJECT_ITEM) return WXL_FALSE;
        *out = Obj(object)->position;
        return WXL_TRUE;
    }

    wxl_bool WXL_CALL FakeVisibleItemId(const void* object, uint32_t slot, int32_t* out)
    {
        if (slot >= 19) return WXL_FALSE;
        *out = Obj(object)->visibleItems[slot];
        return WXL_TRUE;
    }

    int32_t WXL_CALL FakeVisibleItemIdOf(const void* item)
    {
        return static_cast<const FakeVisibleItem*>(item)->itemId;
    }

    int32_t WXL_CALL FakeTileX(const void* area) { return static_cast<const FakeArea*>(area)->tileX; }
    int32_t WXL_CALL FakeTileY(const void* area) { return static_cast<const FakeArea*>(area)->tileY; }

    // --- A fake script engine: a value stack, globals, an event table, as the script rows see them

    struct FakeValue
    {
        uint32_t    type = WXL_VALUE_NIL;
        double      number = 0.0;
        std::string string;
        bool        boolean = false;
    };

    /// A global the runtime set: the C closure and the slot in its upvalue.
    struct FakeClosure
    {
        wxl_script_native_fn fn   = nullptr;
        uint32_t             slot = 0;
    };

    struct FakeSignal
    {
        uint32_t               index = 0;
        std::vector<FakeValue> args;
    };

    struct FakeVm
    {
        bool                                  live = false;
        std::vector<FakeValue>                stack;
        std::map<std::string, FakeClosure>    closures;      ///< globals the runtime set
        std::set<std::string>                 clientGlobals; ///< globals the client set
        uint32_t                              runningSlot = 0;
        std::vector<std::string>              errors;
        std::vector<std::string>              ran;
        std::vector<std::vector<std::string>> eventTables; ///< every table installed, in order
        std::vector<FakeSignal>               signals;
    };

    FakeVm  g_vm;
    int32_t g_uiInitialized = 0;
    uint8_t g_uiLoggingIn   = 0;

    FakeVm* Vm(void* state) { return static_cast<FakeVm*>(state); }

    /// The stack slot a positive Lua index names, or null.
    const FakeValue* At(void* state, int32_t index)
    {
        FakeVm* vm = Vm(state);
        if (!vm || index <= 0 || static_cast<size_t>(index) > vm->stack.size()) return nullptr;
        return &vm->stack[static_cast<size_t>(index) - 1];
    }

    void* WXL_CALL FakeScriptState(void) { return g_vm.live ? &g_vm : nullptr; }
    int32_t WXL_CALL FakeUiInitialized(void) { return g_uiInitialized; }
    uint8_t WXL_CALL FakeUiLoggingIn(void) { return g_uiLoggingIn; }

    wxl_bool WXL_CALL FakeGlobalIsSet(void* state, const char* name)
    {
        return Vm(state)->closures.count(name) || Vm(state)->clientGlobals.count(name) ? WXL_TRUE
                                                                                      : WXL_FALSE;
    }

    void WXL_CALL FakeSetGlobalClosure(void* state, const char* name, wxl_script_native_fn fn, uint32_t slot)
    {
        Vm(state)->closures[name] = FakeClosure{fn, slot};
    }

    void WXL_CALL FakeClearGlobal(void* state, const char* name) { Vm(state)->closures.erase(name); }
    uint32_t WXL_CALL FakeClosureSlot(void* state) { return Vm(state)->runningSlot; }
    int32_t WXL_CALL FakeTop(void* state) { return static_cast<int32_t>(Vm(state)->stack.size()); }

    uint32_t WXL_CALL FakeValueType(void* state, int32_t index)
    {
        const FakeValue* value = At(state, index);
        return value ? value->type : WXL_VALUE_NIL;
    }

    double WXL_CALL FakeToNumber(void* state, int32_t index)
    {
        const FakeValue* value = At(state, index);
        return value ? value->number : 0.0;
    }

    const char* WXL_CALL FakeToString(void* state, int32_t index, uint32_t* length)
    {
        const FakeValue* value = At(state, index);
        if (!value) return nullptr;
        if (length) *length = static_cast<uint32_t>(value->string.size());
        return value->string.c_str();
    }

    wxl_bool WXL_CALL FakeToBool(void* state, int32_t index)
    {
        const FakeValue* value = At(state, index);
        return value && value->boolean ? WXL_TRUE : WXL_FALSE;
    }

    void WXL_CALL FakePushNumber(void* state, double number)
    {
        FakeValue value;
        value.type   = WXL_VALUE_NUMBER;
        value.number = number;
        Vm(state)->stack.push_back(value);
    }

    void WXL_CALL FakePushString(void* state, const char* text, uint32_t length)
    {
        FakeValue value;
        value.type   = WXL_VALUE_STRING;
        value.string = std::string(text, length);
        Vm(state)->stack.push_back(value);
    }

    void WXL_CALL FakePushBool(void* state, wxl_bool flag)
    {
        FakeValue value;
        value.type    = WXL_VALUE_BOOL;
        value.boolean = flag != WXL_FALSE;
        Vm(state)->stack.push_back(value);
    }

    void WXL_CALL FakePushNil(void* state) { Vm(state)->stack.push_back(FakeValue{}); }

    void WXL_CALL FakePop(void* state, int32_t count)
    {
        std::vector<FakeValue>& stack = Vm(state)->stack;
        const size_t            n     = count > 0 ? static_cast<size_t>(count) : 0;
        stack.resize(stack.size() > n ? stack.size() - n : 0);
    }

    /// The fake records the error and returns, where the client's would not; the thunk is
    /// written so that it returns nothing to the caller either way.
    void WXL_CALL FakeError(void* state, const char* message) { Vm(state)->errors.push_back(message); }

    void WXL_CALL FakeRun(const char* source, const char* name)
    {
        g_vm.ran.push_back(std::string(name) + ": " + source);
    }

    void WXL_CALL FakeCreateEvents(const char** names, uint32_t count)
    {
        g_vm.eventTables.push_back(std::vector<std::string>(names, names + count));
    }

    void WXL_CALL FakeSignalEvent(uint32_t index, void* state, int32_t argCount)
    {
        FakeSignal signal;
        signal.index                  = index;
        std::vector<FakeValue>& stack = Vm(state)->stack;
        const size_t            n     = argCount > 0 ? static_cast<size_t>(argCount) : 0;
        signal.args.assign(stack.end() - static_cast<std::ptrdiff_t>(n), stack.end());
        Vm(state)->signals.push_back(signal);
    }

    /// Calls a global the runtime set, as the client's Lua would: the arguments on the stack,
    /// the closure's slot in its upvalue, the results being whatever the function left on top.
    std::vector<FakeValue> CallGlobal(const char* name, std::vector<FakeValue> args, bool* found)
    {
        const auto it = g_vm.closures.find(name);
        *found         = it != g_vm.closures.end();
        if (!*found) return {};
        g_vm.stack       = std::move(args);
        g_vm.runningSlot = it->second.slot;
        const int32_t pushed = it->second.fn(&g_vm);
        const size_t  n      = pushed > 0 ? static_cast<size_t>(pushed) : 0;
        std::vector<FakeValue> results(g_vm.stack.end() - static_cast<std::ptrdiff_t>(n), g_vm.stack.end());
        g_vm.stack.clear();
        return results;
    }

    FakeValue Number(double number)
    {
        FakeValue value;
        value.type   = WXL_VALUE_NUMBER;
        value.number = number;
        return value;
    }

    FakeValue String(const char* text)
    {
        FakeValue value;
        value.type   = WXL_VALUE_STRING;
        value.string = text;
        return value;
    }

    /// The accessors the fake client hands over: what the seams and the checks below read.
    /// A message as the client holds one: a buffer, its end, and where reading starts.
    struct FakeStore
    {
        const uint8_t* data = nullptr;
        uint32_t       size = 0;
        uint32_t       read = 0;
    };

    const void* WXL_CALL FakeMessageData(const void* store)
    {
        return store ? static_cast<const FakeStore*>(store)->data : nullptr;
    }

    uint32_t WXL_CALL FakeMessageSize(const void* store)
    {
        return store ? static_cast<const FakeStore*>(store)->size : 0u;
    }

    uint32_t WXL_CALL FakeMessageRead(const void* store)
    {
        return store ? static_cast<const FakeStore*>(store)->read : 0u;
    }

    /// One file the fake client holds, and the cursor a reader is at.
    struct FakeFile
    {
        std::string name;
        std::string bytes;
        uint64_t    at    = 0;
        bool        open  = false;
    };

    std::vector<FakeFile> g_files;
    int                   g_opens  = 0;
    int                   g_closes = 0;

    FakeFile* FileNamed(const char* name)
    {
        for (FakeFile& file : g_files)
            if (file.name == name) return &file;
        return nullptr;
    }

    wxl_bool WXL_CALL FakeFileExists(const char* path)
    {
        return path && FileNamed(path) ? WXL_TRUE : WXL_FALSE;
    }

    void* WXL_CALL FakeFileOpen(const char* path)
    {
        FakeFile* file = path ? FileNamed(path) : nullptr;
        if (!file) return nullptr;
        file->at   = 0;
        file->open = true;
        ++g_opens;
        return file;
    }

    uint64_t WXL_CALL FakeFileSize(void* file)
    {
        return file ? static_cast<const FakeFile*>(file)->bytes.size() : 0u;
    }

    wxl_bool WXL_CALL FakeFileSeek(void* file, uint64_t offset)
    {
        if (!file) return WXL_FALSE;
        static_cast<FakeFile*>(file)->at = offset;
        return WXL_TRUE;
    }

    uint32_t WXL_CALL FakeFileRead(void* handle, void* buffer, uint32_t count)
    {
        FakeFile* file = static_cast<FakeFile*>(handle);
        if (!file || !buffer || file->at >= file->bytes.size()) return 0;
        const uint64_t left = file->bytes.size() - file->at;
        const uint32_t got  = left < count ? static_cast<uint32_t>(left) : count;
        memcpy(buffer, file->bytes.data() + file->at, got);
        file->at += got;
        return got;
    }

    void WXL_CALL FakeFileClose(void* file)
    {
        if (!file) return;
        static_cast<FakeFile*>(file)->open = false;
        ++g_closes;
    }

    wxl_bool WXL_CALL FakeArchiveMount(const char*, int32_t) { return WXL_TRUE; }

    // The graphics device, as the paint and the device's resets see it.
    bool     g_gxContext = true;
    bool     g_gxVisible = true;
    bool     g_gxDevice  = true;
    uint32_t g_gxWidth   = 1920;
    uint32_t g_gxHeight  = 1080;

    wxl_bool WXL_CALL FakeGxDeviceExists() { return g_gxDevice ? WXL_TRUE : WXL_FALSE; }
    wxl_bool WXL_CALL FakeGxHasContext() { return g_gxDevice && g_gxContext ? WXL_TRUE : WXL_FALSE; }
    wxl_bool WXL_CALL FakeGxWindowVisible() { return g_gxDevice && g_gxVisible ? WXL_TRUE : WXL_FALSE; }

    wxl_bool WXL_CALL FakeGxWindowSize(uint32_t* width, uint32_t* height)
    {
        if (!g_gxDevice || !width || !height) return WXL_FALSE;
        *width  = g_gxWidth;
        *height = g_gxHeight;
        return WXL_TRUE;
    }

    /// Every other row stays null, which is what a client that hands nothing over looks like.
    wxl_core_init FakeInit()
    {
        wxl_core_init init{};
        init.struct_size            = sizeof init;
        init.map_id                 = &FakeMapId;
        init.local_player_object    = &FakeLocalPlayer;
        init.ui_target_guid         = &FakeTargetGuid;
        init.object_guid            = &FakeGuid;
        init.object_type_id         = &FakeTypeId;
        init.object_entry           = &FakeEntry;
        init.object_position        = &FakePosition;
        init.unit_health            = &FakeHealth;
        init.player_visible_item_id = &FakeVisibleItemId;
        init.visible_item_id        = &FakeVisibleItemIdOf;
        init.map_area_tile_x        = &FakeTileX;
        init.map_area_tile_y        = &FakeTileY;
        init.ui_initialized         = &FakeUiInitialized;
        init.ui_logging_in          = &FakeUiLoggingIn;
        init.script_state           = &FakeScriptState;
        init.script_global_is_set   = &FakeGlobalIsSet;
        init.script_set_global_closure = &FakeSetGlobalClosure;
        init.script_clear_global    = &FakeClearGlobal;
        init.script_closure_slot    = &FakeClosureSlot;
        init.script_top             = &FakeTop;
        init.script_value_type      = &FakeValueType;
        init.script_to_number       = &FakeToNumber;
        init.script_to_string       = &FakeToString;
        init.script_to_bool         = &FakeToBool;
        init.script_push_number     = &FakePushNumber;
        init.script_push_string     = &FakePushString;
        init.script_push_bool       = &FakePushBool;
        init.script_push_nil        = &FakePushNil;
        init.script_pop             = &FakePop;
        init.script_error           = &FakeError;
        init.script_run             = &FakeRun;
        init.script_create_events   = &FakeCreateEvents;
        init.script_signal_event    = &FakeSignalEvent;
        init.message_data           = &FakeMessageData;
        init.message_size           = &FakeMessageSize;
        init.message_read           = &FakeMessageRead;
        init.file_exists            = &FakeFileExists;
        init.file_open              = &FakeFileOpen;
        init.file_size              = &FakeFileSize;
        init.file_seek              = &FakeFileSeek;
        init.file_read              = &FakeFileRead;
        init.file_close             = &FakeFileClose;
        init.archive_mount          = &FakeArchiveMount;
        init.gx_device_exists       = &FakeGxDeviceExists;
        init.gx_has_context         = &FakeGxHasContext;
        init.gx_window_visible      = &FakeGxWindowVisible;
        init.gx_window_size         = &FakeGxWindowSize;
        return init;
    }
}

// --- What a mod receives ----------------------------------------------------------------------

namespace
{
    /// One event as a handler saw it: which, and a copy of the bytes it was handed.
    struct Received
    {
        wxl_event         event;
        std::vector<char> args;

        template <class T>
        T As() const
        {
            T typed{};
            if (args.size() >= sizeof typed) memcpy(&typed, args.data(), sizeof typed);
            return typed;
        }
    };

    std::vector<Received> g_received;

    /// The size of the struct each event carries, so the harness can keep a copy of it.
    size_t ArgsSize(wxl_event event)
    {
        switch (event)
        {
#define WXL_EVENT(family, name, id, args) case WXL_EV_##name: return sizeof(args);
#include "wxl/events.def"
#undef WXL_EVENT
        default: return 0;
        }
    }

    /// One tag per event id; a subscription's `user` is the address of its own, which is how a
    /// handler that is only handed `user` and `args` knows which event arrived.
    wxl_event g_tags[WXL_EV_COUNT]{};

    void WXL_CALL OnEvent(void* user, const void* args)
    {
        const wxl_event event = *static_cast<const wxl_event*>(user);
        Received record{ event, {} };
        const size_t size = ArgsSize(event);
        if (args && size)
        {
            record.args.resize(size);
            memcpy(record.args.data(), args, size);
        }
        g_received.push_back(std::move(record));
    }

    int g_failures = 0;

    void Check(bool ok, const char* what)
    {
        printf("%-4s %s\n", ok ? "ok" : "FAIL", what);
        if (!ok) ++g_failures;
    }

    /// How many of an event arrived.
    size_t Count(wxl_event event)
    {
        size_t n = 0;
        for (const Received& record : g_received) n += record.event == event;
        return n;
    }

    /// The one event received, or null when none or more than one arrived.
    const Received* Only(wxl_event event)
    {
        const Received* found = nullptr;
        for (const Received& record : g_received)
        {
            if (record.event != event) continue;
            if (found) return nullptr;
            found = &record;
        }
        return found;
    }
}

// --- The checks -------------------------------------------------------------------------------

namespace
{
    /// How far the client has booted: announced once each, in order, gaps left as gaps.
    void CheckStages()
    {
        namespace stages = wxl::core::stages;
        stages::Restart();

        stages::Reach(WXL_STAGE_EARLY);
        Check(stages::Reached() == WXL_STAGE_EARLY, "the first stage is announced");

        stages::Reach(WXL_STAGE_STORAGE);
        Check(stages::Reached() == WXL_STAGE_STORAGE, "and the next one after it");

        stages::Reach(WXL_STAGE_STORAGE);
        stages::Reach(WXL_STAGE_EARLY);
        Check(stages::Reached() == WXL_STAGE_STORAGE,
              "a stage already announced, and one behind it, are both ignored");

        stages::Reach(WXL_STAGE_SCRIPT);
        Check(stages::Reached() == WXL_STAGE_SCRIPT,
              "a stage that was never reached is skipped, not invented: with no device brought "
              "up, script follows storage and graphics is a gap");

        stages::Restart();
        Check(stages::Reached() == WXL_STAGE_EARLY, "a runtime that starts over starts over");

        stages::Reach(WXL_STAGE_EARLY);
        std::thread([] { stages::Reach(WXL_STAGE_STORAGE); }).join();
        Check(stages::Reached() == WXL_STAGE_EARLY,
              "a stage reached on another thread is not announced there");
        stages::Reach(WXL_STAGE_STORAGE);
        Check(stages::Reached() == WXL_STAGE_STORAGE, "the main thread announces it on its next pass");
        stages::Restart();
    }

    /// The rows a client hands over decide whether its accessors are called at all.
    void CheckInitRows()
    {
        namespace binding = wxl::core::binding;
        char why[512] = {};

        wxl_core_init init = FakeInit();
        init.row_count     = WXL_CORE_INIT_ROW_COUNT;
        init.rows          = wxl_core_init_rows;
        Check(binding::Agrees(init, why, sizeof why), "a client built from the same table agrees");

        const wxl_core_init bare = FakeInit();
        Check(!binding::Agrees(bare, why, sizeof why) && strstr(why, "no rows"),
              "one that hands over no rows is refused");

        std::vector<const char*> rows(wxl_core_init_rows, wxl_core_init_rows + WXL_CORE_INIT_ROW_COUNT);
        rows[5]   = "something: else ()";
        init.rows = rows.data();
        Check(!binding::Agrees(init, why, sizeof why) && strstr(why, "row 5 differs") &&
                  strstr(why, wxl_core_init_rows[5]),
              "one row that differs is refused, naming both sides of it");

        init.rows      = wxl_core_init_rows;
        init.row_count = WXL_CORE_INIT_ROW_COUNT - 3;
        Check(binding::Agrees(init, why, sizeof why), "an older client, with fewer rows, agrees");

        init.row_count   = WXL_CORE_INIT_ROW_COUNT;
        init.struct_size = static_cast<uint32_t>(offsetof(wxl_core_init, map_id) + 8 * sizeof(void*));
        Check(!binding::Agrees(init, why, sizeof why) && strstr(why, "struct_size"),
              "rows that its struct_size does not hold are refused");
    }

    /// Every event the seam table says some body raises is one the host will let a mod subscribe
    /// to. A seam that raises an event the attachment never published is a silent dead event.
    void CheckEveryRaisedEventIsSubscribable()
    {
        for (const wxl_event event : wxl::core::seams::RaisedEvents())
        {
            g_tags[event] = event;
            const wxl_subscription subscription =
                g_host->Events().Subscribe(event, &OnEvent, &g_tags[event], 0);
            char what[128];
            snprintf(what, sizeof what, "a mod can subscribe to event %u", unsigned(event));
            Check(subscription != WXL_SUBSCRIPTION_NONE, what);
        }
    }

    /// Every row declares a return type and a convention the generator knows.
    void CheckEveryRowDeclaresItsAbi()
    {
        for (const wxl::core::seams::Seam& seam : wxl::core::seams::Table())
        {
            const bool known = strcmp(seam.convention, "cdecl") == 0 ||
                               strcmp(seam.convention, "thiscall") == 0 ||
                               strcmp(seam.convention, "stdcall") == 0 ||
                               strcmp(seam.convention, "regparm") == 0;
            char what[160];
            snprintf(what, sizeof what, "%s declares %s %s %s", seam.name, seam.returns,
                     seam.convention, seam.params);
            Check(known && seam.returns[0] != 0, what);
        }
    }

    /// Event.DispatchUpdate: Before raises Update carrying the tick's own elapsed_sec.
    void CheckEventDispatchUpdate()
    {
        g_received.clear();
        wxl::seam::Event::DispatchUpdate::Before(nullptr, 1000u, 0.125f);

        const Received* update = Only(WXL_EV_Update);
        Check(update != nullptr, "Event.DispatchUpdate raises Update exactly once");
        if (!update) return;
        Check(update->As<wxl_update_args>().delta == 0.125f,
              "Update carries the seam's elapsed_sec as its delta");
    }

    /// World.Preload: the pair raises WorldEnter on the way in and WorldLeave only once a world
    /// has been entered, and neither when the loading screen is not being shown.
    void CheckWorldPreload()
    {
        const wxl_vec3 where{ 1.0f, 2.0f, 3.0f };
        g_mapId = 571;

        // A preload with no loading screen is a far-sight, cinematic or game-object preload.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 0);
        wxl::seam::World::Preload::After(&where, 0);
        Check(g_received.empty(), "World.Preload raises nothing when show_loading is clear");

        // The first real preload enters a world; there is no world to leave yet.
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldLeave) == nullptr, "the first world entry raises no WorldLeave");

        const Received* entered = Only(WXL_EV_WorldEnter);
        Check(entered != nullptr, "World.Preload raises WorldEnter once a world is loaded");
        if (entered)
        {
            const wxl_world_args args = entered->As<wxl_world_args>();
            Check(args.map_id == 571u, "WorldEnter carries the map the binding reports");
            Check(args.position.x == 1.0f && args.position.y == 2.0f && args.position.z == 3.0f,
                  "WorldEnter carries the position the seam was called with");
        }

        // The next one leaves the world just entered, naming it, and enters the new one.
        g_mapId = 0;
        g_received.clear();
        wxl::seam::World::Preload::Before(&where, 1);
        const Received* left = Only(WXL_EV_WorldLeave);
        Check(left != nullptr, "the next world entry raises WorldLeave first");
        if (left)
            Check(left->As<wxl_world_args>().map_id == 571u,
                  "WorldLeave names the map being left, not the one arriving");
        wxl::seam::World::Preload::After(&where, 1);
        Check(Only(WXL_EV_WorldEnter) != nullptr, "and WorldEnter after it");
    }

    /// ObjectMgr: InitObject's After tracks and announces; ObjDelete's Before announces and
    /// untracks, after which the handle a mod kept reads as absent; DestroyRemovedObject
    /// announces only what is still tracked.
    void CheckObjectMgr()
    {
        FakeObject boar;
        boar.guid     = 0xF130000000000123ull;
        boar.typeId   = WXL_OBJECT_UNIT;
        boar.entry    = 113;
        boar.position = { 4.0f, 5.0f, 6.0f };
        boar.health   = 90;

        g_received.clear();
        wxl::seam::ObjectMgr::InitObject::After(&boar, nullptr, 1000u);

        const Received* created = Only(WXL_EV_ObjectCreate);
        Check(created != nullptr, "ObjectMgr.InitObject raises ObjectCreate exactly once");
        if (!created) return;
        const wxl_object_args args = created->As<wxl_object_args>();
        Check(args.object != WXL_HANDLE_NONE, "ObjectCreate carries a handle");
        Check(args.guid == boar.guid, "ObjectCreate carries the object's guid, read through the binding");
        Check(args.type == WXL_OBJECT_UNIT, "ObjectCreate carries the client's type id");

        const wxl_object_api* api = wxl::core::objects::Api();
        Check(api->valid(args.object) == WXL_TRUE, "the handle resolves through wxl.object");
        Check(api->guid(args.object) == boar.guid, "and reads the guid");
        uint32_t health = 0;
        Check(api->unit_health(args.object, &health) == WXL_OK && health == 90,
              "a unit read goes through the binding to the client's object");
        wxl_vec3 position{};
        Check(api->position(args.object, &position) == WXL_OK && position.y == 5.0f,
              "so does a position");
        Check(api->from_guid(boar.guid) == args.object, "the guid resolves back to the handle");

        // Announcing the same object again tracks it once.
        g_received.clear();
        wxl::seam::ObjectMgr::InitObject::After(&boar, nullptr, 1001u);
        Check(Only(WXL_EV_ObjectCreate) != nullptr &&
                  Only(WXL_EV_ObjectCreate)->As<wxl_object_args>().object == args.object,
              "a second InitObject on the same object answers the same handle");

        // ObjDelete: announced while resident, then absent.
        g_received.clear();
        wxl::seam::ObjectMgr::ObjDelete::Before(&boar);
        const Received* destroyed = Only(WXL_EV_ObjectDestroy);
        Check(destroyed != nullptr, "ObjectMgr.ObjDelete raises ObjectDestroy exactly once");
        if (destroyed)
        {
            Check(destroyed->As<wxl_object_args>().object == args.object,
                  "ObjectDestroy carries the handle ObjectCreate did");
            Check(destroyed->As<wxl_object_args>().guid == boar.guid, "and its guid");
        }
        Check(api->valid(args.object) == WXL_FALSE, "after ObjDelete the handle no longer resolves");
        Check(api->unit_health(args.object, &health) == WXL_E_NOT_FOUND,
              "a read through the stale handle is absent, not the object's last value");
        Check(api->from_guid(boar.guid) == WXL_HANDLE_NONE, "and the guid resolves to nothing");

        // DestroyRemovedObject on what ObjDelete already let go: nothing.
        g_received.clear();
        wxl::seam::ObjectMgr::DestroyRemovedObject::Before(&boar);
        Check(g_received.empty(), "DestroyRemovedObject after ObjDelete raises nothing");

        // DestroyRemovedObject on a tracked object (the DestroyAllObjects path): once.
        FakeObject chest;
        chest.guid   = 0xF110000000000456ull;
        chest.typeId = WXL_OBJECT_GAMEOBJECT;
        wxl::seam::ObjectMgr::InitObject::After(&chest, nullptr, 1002u);
        g_received.clear();
        wxl::seam::ObjectMgr::DestroyRemovedObject::Before(&chest);
        Check(Only(WXL_EV_ObjectDestroy) != nullptr &&
                  Only(WXL_EV_ObjectDestroy)->As<wxl_object_args>().type == WXL_OBJECT_GAMEOBJECT,
              "DestroyRemovedObject on a tracked object raises ObjectDestroy once");
        Check(api->valid(Only(WXL_EV_ObjectDestroy)->As<wxl_object_args>().object) == WXL_FALSE,
              "and the handle is gone after it");
    }

    /// Unit: OnDeath's After raises UnitDeath for a tracked unit; the aura pair raises
    /// UnitAuraChanged with the spell and the direction; an untracked unit raises nothing.
    void CheckUnit()
    {
        FakeObject wolf;
        wolf.guid   = 0xF130000000000789ull;
        wolf.typeId = WXL_OBJECT_UNIT;
        wxl::seam::ObjectMgr::InitObject::After(&wolf, nullptr, 2000u);
        const wxl_handle handle = wxl::core::objects::HandleOf(&wolf);

        g_received.clear();
        wxl::seam::Unit::OnDeath::After(&wolf);
        Check(Only(WXL_EV_UnitDeath) != nullptr &&
                  Only(WXL_EV_UnitDeath)->As<wxl_unit_args>().unit == handle,
              "Unit.OnDeath raises UnitDeath once with the unit's handle");

        g_received.clear();
        wxl::seam::Unit::OnAuraAdded::After(&wolf, 3u, 0, nullptr, 8921);
        const Received* added = Only(WXL_EV_UnitAuraChanged);
        Check(added != nullptr, "Unit.OnAuraAdded raises UnitAuraChanged once");
        if (added)
        {
            const wxl_unit_aura_args args = added->As<wxl_unit_aura_args>();
            Check(args.unit == handle && args.spell_id == 8921u && args.removed == WXL_FALSE,
                  "with the unit, the spell, and removed clear");
        }

        g_received.clear();
        wxl::seam::Unit::OnAuraRemoved::After(&wolf, 3u, 0, nullptr, 8921);
        const Received* removed = Only(WXL_EV_UnitAuraChanged);
        Check(removed != nullptr && removed->As<wxl_unit_aura_args>().removed == WXL_TRUE,
              "Unit.OnAuraRemoved raises UnitAuraChanged once with removed set");

        FakeObject stranger;
        stranger.guid   = 0xF13000000000ABCDull;
        stranger.typeId = WXL_OBJECT_UNIT;
        g_received.clear();
        wxl::seam::Unit::OnDeath::After(&stranger);
        wxl::seam::Unit::OnAuraAdded::After(&stranger, 0u, 0, nullptr, 1);
        Check(g_received.empty(), "a unit the runtime never tracked raises nothing");

        wxl::seam::ObjectMgr::ObjDelete::Before(&wolf);
    }

    /// Player: UpdateInventoryComponent raises EquipmentChanged when the slot's item id moved,
    /// with the new id, and nothing for an enchant-only refresh.
    void CheckPlayer()
    {
        FakeObject me;
        me.guid            = 0x0000000000000042ull;
        me.typeId          = WXL_OBJECT_PLAYER;
        me.visibleItems[15] = 19019;
        wxl::seam::ObjectMgr::InitObject::After(&me, nullptr, 3000u);
        const wxl_handle handle = wxl::core::objects::HandleOf(&me);

        FakeVisibleItem old;
        old.itemId = 0;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        const Received* equipped = Only(WXL_EV_EquipmentChanged);
        Check(equipped != nullptr, "Player.UpdateInventoryComponent raises EquipmentChanged once when the slot filled");
        if (equipped)
        {
            const wxl_equipment_args args = equipped->As<wxl_equipment_args>();
            Check(args.unit == handle && args.slot == 15u && args.item_id == 19019u,
                  "with the player's handle, the slot and the new item id");
        }

        old.itemId = 19019;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        Check(g_received.empty(), "the same id again (an enchant refresh) raises nothing");

        me.visibleItems[15] = 0;
        g_received.clear();
        wxl::seam::Player::UpdateInventoryComponent::Before(&me, 15, &old);
        wxl::seam::Player::UpdateInventoryComponent::After(&me, 15, &old);
        Check(Only(WXL_EV_EquipmentChanged) != nullptr &&
                  Only(WXL_EV_EquipmentChanged)->As<wxl_equipment_args>().item_id == 0u,
              "clearing the slot raises EquipmentChanged with an item id of 0");

        wxl::seam::ObjectMgr::ObjDelete::Before(&me);
    }

    /// GameUI: the target is announced once per change, with the previous and current handles,
    /// through SendTarget after Target's internal clear, and through a standalone ClearTarget.
    void CheckGameUI()
    {
        FakeObject rat;
        rat.guid   = 0xF130000000000AAAull;
        rat.typeId = WXL_OBJECT_UNIT;
        FakeObject bat;
        bat.guid   = 0xF130000000000BBBull;
        bat.typeId = WXL_OBJECT_UNIT;
        wxl::seam::ObjectMgr::InitObject::After(&rat, nullptr, 4000u);
        wxl::seam::ObjectMgr::InitObject::After(&bat, nullptr, 4001u);
        const wxl_handle ratHandle = wxl::core::objects::HandleOf(&rat);
        const wxl_handle batHandle = wxl::core::objects::HandleOf(&bat);

        // CGGameUI::Target(rat): ClearTarget(old, 0), the static written, SendTarget(rat).
        g_received.clear();
        wxl::seam::GameUI::ClearTarget::After(0, 0);
        g_targetGuid = rat.guid;
        wxl::seam::GameUI::SendTarget::After(rat.guid);
        const Received* first = Only(WXL_EV_TargetChanged);
        Check(first != nullptr, "targeting from nothing raises TargetChanged once");
        if (first)
            Check(first->As<wxl_target_args>().previous == WXL_HANDLE_NONE &&
                      first->As<wxl_target_args>().current == ratHandle,
                  "from the null handle to the target's");

        // CGGameUI::Target(bat): the internal clear is not announced, the change is, once.
        g_received.clear();
        g_targetGuid = 0;
        wxl::seam::GameUI::ClearTarget::After(rat.guid, 0);
        Check(g_received.empty(), "Target's own pre-clear (a2 clear) announces nothing");
        g_targetGuid = bat.guid;
        wxl::seam::GameUI::SendTarget::After(bat.guid);
        const Received* second = Only(WXL_EV_TargetChanged);
        Check(second != nullptr, "retargeting raises TargetChanged once");
        if (second)
            Check(second->As<wxl_target_args>().previous == ratHandle &&
                      second->As<wxl_target_args>().current == batHandle,
                  "from the previous target's handle to the new one's");

        // The summon-friend script: SendTarget with the static unchanged announces nothing.
        g_received.clear();
        wxl::seam::GameUI::SendTarget::After(rat.guid);
        Check(g_received.empty(), "SendTarget without a change of the static announces nothing");

        // A standalone ClearTarget (a2 set): announced, to the null handle.
        g_received.clear();
        g_targetGuid = 0;
        wxl::seam::GameUI::ClearTarget::After(bat.guid, 1);
        const Received* cleared = Only(WXL_EV_TargetChanged);
        Check(cleared != nullptr, "a standalone ClearTarget raises TargetChanged once");
        if (cleared)
            Check(cleared->As<wxl_target_args>().previous == batHandle &&
                      cleared->As<wxl_target_args>().current == WXL_HANDLE_NONE,
                  "from the target's handle to the null handle");

        wxl::seam::ObjectMgr::ObjDelete::Before(&rat);
        wxl::seam::ObjectMgr::ObjDelete::Before(&bat);
    }

    /// MapArea: PrepareChunk's Before raises TerrainChunkBuild with the map, the tile and the chunk.
    void CheckMapArea()
    {
        FakeArea area;
        area.tileX = 32;
        area.tileY = 48;
        g_mapId    = 0;

        g_received.clear();
        wxl::seam::MapArea::PrepareChunk::Before(&area, 3, 9);
        const Received* built = Only(WXL_EV_TerrainChunkBuild);
        Check(built != nullptr, "MapArea.PrepareChunk raises TerrainChunkBuild once");
        if (!built) return;
        const wxl_terrain_chunk_args args = built->As<wxl_terrain_chunk_args>();
        Check(args.map_id == 0u && args.tile_x == 32 && args.tile_y == 48,
              "with the map and the tile index read through the binding");
        Check(args.chunk_x == 3 && args.chunk_y == 9, "and the chunk the call named");
    }

    // --- wxl.script: what a mod registers, called back through the fake engine -----------------

    wxl_str Str(const char* text) { return wxl_str{text, static_cast<uint32_t>(strlen(text))}; }

    const wxl_script_api& Script() { return wxl::core::services::ScriptApi(); }

    int      g_greetCalls      = 0;
    uint32_t g_greetArgTypes[3]{};
    bool     g_greetWrongTypeMissed = false;

    /// Greet(n, name): answers "hi <name>", n * 2, true, nil; fails on anything else.
    void WXL_CALL Greet(wxl_script_call* call, void* user)
    {
        const wxl_script_api& api = Script();
        if (user) *static_cast<int*>(user) += 1;

        double  n = 0.0;
        wxl_str s{nullptr, 0};
        if (api.arg_count(call) != 2 || !api.arg_number(call, 0, &n) || !api.arg_string(call, 1, &s))
        {
            api.fail(call, Str("usage: Greet(number, string)"));
            return;
        }
        for (uint32_t i = 0; i < 3; ++i) g_greetArgTypes[i] = api.arg_type(call, i);
        wxl_bool flag = WXL_FALSE;
        wxl_str  text{nullptr, 0};
        g_greetWrongTypeMissed = api.arg_bool(call, 0, &flag) == WXL_FALSE &&
                                 api.arg_string(call, 0, &text) == WXL_FALSE &&
                                 api.arg_number(call, 1, &n) == WXL_FALSE && n == 21.0;

        char line[64];
        snprintf(line, sizeof line, "hi %.*s", static_cast<int>(s.size), s.data);
        api.push_string(call, Str(line));
        api.push_number(call, n * 2);
        api.push_bool(call, WXL_TRUE);
        api.push_nil(call);
    }

    /// Fails after pushing: the push must not reach the caller.
    void WXL_CALL Failing(wxl_script_call* call, void*)
    {
        Script().push_number(call, 1.0);
        Script().fail(call, Str("nope"));
    }

#if defined(_MSC_VER)
    void WXL_CALL Crashing(wxl_script_call*, void*)
    {
        volatile int* nowhere = nullptr;
        *nowhere              = 1;
    }
#endif

    /// A mod's ScriptVmReady handler: registers its function on every new context and keeps
    /// the answer in `user`.
    void WXL_CALL OnVmReadyRegister(void* user, const void*)
    {
        *static_cast<wxl_result*>(user) = Script().register_function("WxlHarnessGreet", &Greet, &g_greetCalls);
    }

    /// FrameScript: Initialize's After raises ScriptVmReady naming the context CGGameUI's state
    /// implies, Destroy's Before raises ScriptVmClosing naming the same one; the service takes
    /// registrations only between the two, drives calls through the engine, and starts over on
    /// the next context.
    void CheckFrameScript()
    {
        const wxl_script_api& api = Script();
        Check(g_host->Services().Get(WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION) == &api,
              "wxl.script is published to mods");
        Check(api.struct_size == sizeof(wxl_script_api), "with the layout script.h declares");

        Check(api.register_function("WxlHarnessEarly", &Greet, nullptr) == WXL_E_FAILED,
              "register_function before any context exists is refused");
        Check(api.register_event("WXL_HARNESS_EARLY") == WXL_E_FAILED &&
                  api.fire_event("WXL_HARNESS_EARLY", nullptr, 0) == WXL_E_FAILED &&
                  api.run(Str("x = 1"), "early") == WXL_E_FAILED,
              "so are register_event, fire_event and run");

        wxl_result registered = WXL_E_FAILED;
        const wxl_subscription onReady =
            g_host->Events().Subscribe(WXL_EV_ScriptVmReady, &OnVmReadyRegister, &registered, 0);

        // The login-side context: CGGameUI is neither logging in nor initialised.
        g_vm.live       = true;
        g_uiInitialized = 0;
        g_uiLoggingIn   = 0;
        g_received.clear();
        wxl::seam::FrameScript::Initialize::After(1, 0);
        const Received* ready = Only(WXL_EV_ScriptVmReady);
        Check(ready != nullptr, "FrameScript.Initialize raises ScriptVmReady once");
        if (ready)
            Check(ready->As<wxl_script_vm_args>().context == WXL_SCRIPT_CONTEXT_GLUE &&
                      ready->As<wxl_script_vm_args>().native_state == &g_vm,
                  "it says glue while CGGameUI is neither logging in nor initialised, and carries the client's state");
        Check(api.context() == WXL_SCRIPT_CONTEXT_GLUE, "context() says the same");
        Check(registered == WXL_OK && g_vm.closures.count("WxlHarnessGreet") == 1,
              "a function registered from the ScriptVmReady handler is a global of that context");
        Check(api.register_function("WxlHarnessGreet", &Greet, nullptr) == WXL_E_INVALID,
              "registering a name again is refused");
        g_vm.clientGlobals.insert("GetTime");
        Check(api.register_function("GetTime", &Greet, nullptr) == WXL_E_INVALID,
              "a global the client set is not shadowed");
        Check(api.register_function(nullptr, &Greet, nullptr) == WXL_E_INVALID &&
                  api.register_function("", &Greet, nullptr) == WXL_E_INVALID &&
                  api.register_function("WxlHarnessNull", nullptr, nullptr) == WXL_E_INVALID,
              "a null name or function is refused");

        bool found = false;
        std::vector<FakeValue> results = CallGlobal("WxlHarnessGreet", {Number(21.0), String("bob")}, &found);
        Check(found && g_greetCalls == 1, "a call from script reaches the mod's function");
        Check(results.size() == 4 && results[0].type == WXL_VALUE_STRING && results[0].string == "hi bob" &&
                  results[1].type == WXL_VALUE_NUMBER && results[1].number == 42.0 &&
                  results[2].type == WXL_VALUE_BOOL && results[2].boolean &&
                  results[3].type == WXL_VALUE_NIL,
              "with its arguments, and brings back what it pushed, in order");
        Check(g_greetArgTypes[0] == WXL_VALUE_NUMBER && g_greetArgTypes[1] == WXL_VALUE_STRING &&
                  g_greetArgTypes[2] == WXL_VALUE_NIL,
              "arg_type names each argument's type and nil past the last");
        Check(g_greetWrongTypeMissed, "an argument of the wrong type is a miss, not a conversion");
        results = CallGlobal("WxlHarnessGreet", {Number(1.0)}, &found);
        Check(results.empty() && !g_vm.errors.empty() && g_vm.errors.back() == "usage: Greet(number, string)",
              "fail raises its message on the script side once the function returns");

        Check(api.register_function("WxlHarnessFail", &Failing, nullptr) == WXL_OK, "a second function registers");
        results = CallGlobal("WxlHarnessFail", {}, &found);
        Check(found && results.empty() && g_vm.errors.back() == "nope",
              "a function that fails after pushing hands the caller the error, not the results");

        Check(api.run(Str("print(1)"), "harness") == WXL_OK && !g_vm.ran.empty() &&
                  g_vm.ran.back() == "harness: print(1)",
              "run hands the chunk and its name to the engine");

        // Events: one declared before the client installs its table, one after.
        Check(api.register_event("WXL_HARNESS_EVENT") == WXL_OK && g_vm.eventTables.size() == 1 &&
                  g_vm.eventTables.back() == std::vector<std::string>{"WXL_HARNESS_EVENT"},
              "an event declared before the client's table is installed on its own");
        Check(api.register_event("WXL_HARNESS_EVENT") == WXL_OK && g_vm.eventTables.size() == 1,
              "declaring it again changes nothing");
        const char* clientNames[] = {"PLAYER_LOGIN", "PLAYER_LOGOUT"};
        wxl::seam::FrameScript::CreateEvents::After(clientNames, 2);
        Check(g_vm.eventTables.size() == 2 &&
                  g_vm.eventTables.back() == std::vector<std::string>{"PLAYER_LOGIN", "PLAYER_LOGOUT", "WXL_HARNESS_EVENT"},
              "once the client installs its table, FrameScript.CreateEvents installs it again with the declared event behind it");
        Check(api.register_event("WXL_HARNESS_LATE") == WXL_OK && g_vm.eventTables.size() == 3 &&
                  g_vm.eventTables.back() == std::vector<std::string>{"PLAYER_LOGIN", "PLAYER_LOGOUT", "WXL_HARNESS_EVENT", "WXL_HARNESS_LATE"},
              "an event declared afterwards is appended, keeping every index");
        const wxl_value payload[2] = {wxl_value{WXL_VALUE_NUMBER, {7.0}}, wxl_value{WXL_VALUE_STRING, {0.0}}};
        wxl_value       text       = payload[1];
        text.as.string             = Str("x");
        const wxl_value args[2]    = {payload[0], text};
        Check(api.fire_event("WXL_HARNESS_LATE", args, 2) == WXL_OK && g_vm.signals.size() == 1 &&
                  g_vm.signals[0].index == 3 && g_vm.signals[0].args.size() == 2 &&
                  g_vm.signals[0].args[0].number == 7.0 && g_vm.signals[0].args[1].string == "x" &&
                  g_vm.stack.empty(),
              "fire_event signals the event at its index with the values pushed, and leaves the stack as it found it");
        Check(api.fire_event("WXL_HARNESS_NOT_DECLARED", nullptr, 0) == WXL_E_NOT_FOUND,
              "firing an event nobody declared is not found");

#if defined(_MSC_VER)
        Check(api.register_function("WxlHarnessCrash", &Crashing, nullptr) == WXL_OK, "a faulting function registers like any other");
        results = CallGlobal("WxlHarnessCrash", {}, &found);
        Check(found && results.empty() && g_vm.errors.back().find("WxlHarnessCrash faulted (access violation") != std::string::npos,
              "a script function that faults answers the caller with an error naming the fault, and the harness is still here");
        Check(g_vm.closures.count("WxlHarnessCrash") == 0, "and the global is taken away");
        Check(g_vm.closures.count("WxlHarnessGreet") == 1, "the others stay");
#endif

        // Closing: announced with the context Ready named, then nothing is accepted.
        g_received.clear();
        wxl::seam::FrameScript::Destroy::Before();
        const Received* closing = Only(WXL_EV_ScriptVmClosing);
        Check(closing != nullptr && closing->As<wxl_script_vm_args>().context == WXL_SCRIPT_CONTEXT_GLUE,
              "FrameScript.Destroy raises ScriptVmClosing once, naming the context Ready did");
        g_vm.live = false;
        Check(api.register_function("WxlHarnessLate", &Greet, nullptr) == WXL_E_FAILED &&
                  api.fire_event("WXL_HARNESS_LATE", nullptr, 0) == WXL_E_FAILED,
              "after ScriptVmClosing nothing is accepted until the next context");
        {
            // A closure the dead context left behind, called anyway: an error, not the mod.
            g_greetCalls      = 0;
            FakeClosure stale = g_vm.closures["WxlHarnessGreet"];
            g_vm.closures.clear();
            g_vm.runningSlot = stale.slot;
            const int32_t pushed = stale.fn(&g_vm);
            Check(pushed == 0 && g_greetCalls == 0 && g_vm.errors.back().find("no longer provides") != std::string::npos,
                  "a closure outliving its registration answers an error instead of calling the mod");
        }
        g_received.clear();
        wxl::seam::FrameScript::Destroy::Before();
        Check(g_received.empty(), "a Destroy with no context announced raises nothing");

        // The game context: InitializeFrameXML sets m_loggingIn before it creates the context.
        g_vm.live     = true;
        g_uiLoggingIn = 1;
        registered    = WXL_E_FAILED;
        g_received.clear();
        wxl::seam::FrameScript::Initialize::After(1, 1);
        ready = Only(WXL_EV_ScriptVmReady);
        Check(ready != nullptr && ready->As<wxl_script_vm_args>().context == WXL_SCRIPT_CONTEXT_GAME,
              "the context created while CGGameUI is logging in is the game's");
        Check(registered == WXL_OK && g_vm.closures.count("WxlHarnessGreet") == 1,
              "the handler registered again on the new context, and the name was free: the old registration died with its context");
        Check(api.fire_event("WXL_HARNESS_LATE", nullptr, 0) == WXL_E_NOT_FOUND,
              "the events declared on the old context are gone too");
        results = CallGlobal("WxlHarnessGreet", {Number(2.0), String("ann")}, &found);
        Check(found && results.size() == 4 && results[0].string == "hi ann", "and the new registration works");

        // ReloadUI: Flush destroys and recreates with m_initialized still set.
        wxl::seam::FrameScript::Destroy::Before();
        g_vm.closures.clear();
        g_uiLoggingIn   = 0;
        g_uiInitialized = 1;
        g_received.clear();
        wxl::seam::FrameScript::Initialize::After(1, 0);
        ready = Only(WXL_EV_ScriptVmReady);
        Check(ready != nullptr && ready->As<wxl_script_vm_args>().context == WXL_SCRIPT_CONTEXT_GAME,
              "a context recreated while the game interface is initialised is the game's");

        // Back to the glue: ShutdownGame cleared both flags before the flush.
        wxl::seam::FrameScript::Destroy::Before();
        g_vm.closures.clear();
        g_uiInitialized = 0;
        g_received.clear();
        wxl::seam::FrameScript::Initialize::After(1, 0);
        ready = Only(WXL_EV_ScriptVmReady);
        Check(ready != nullptr && ready->As<wxl_script_vm_args>().context == WXL_SCRIPT_CONTEXT_GLUE,
              "a context recreated once the game interface is down is the glue's");

        wxl::seam::FrameScript::Destroy::Before();
        g_vm.live = false;
        g_host->Events().Unsubscribe(onReady);
    }

    // --- NetClient: the gated seams, and what a handler's answer does to the call -------------

    /// What the message handler below saw, and whether it is to stop what it is shown.
    struct MessageWatch
    {
        int      calls  = 0;
        uint32_t opcode = 0;
        uint32_t size   = 0;
        uint8_t  first  = 0;
        bool     stop   = false;
    };

    void WXL_CALL OnMessage(void* user, const void* args)
    {
        MessageWatch&           watch   = *static_cast<MessageWatch*>(user);
        const wxl_message_args& message = *static_cast<const wxl_message_args*>(args);
        ++watch.calls;
        watch.opcode = message.opcode;
        watch.size   = message.size;
        watch.first  = message.size ? static_cast<const uint8_t*>(message.data)[0] : uint8_t(0);
        if (watch.stop && message.suppressed) *message.suppressed = WXL_TRUE;
    }

    void CheckNetClient()
    {
        MessageWatch incoming;
        MessageWatch outgoing;
        const wxl_subscription in =
            g_host->Events().Subscribe(WXL_EV_MessageReceived, &OnMessage, &incoming, 0);
        const wxl_subscription out =
            g_host->Events().Subscribe(WXL_EV_MessageSent, &OnMessage, &outgoing, 0);

        // An incoming message: a uint16 opcode, then three bytes of body.
        const uint8_t received[] = { 0x34, 0x12, 0xAA, 0xBB, 0xCC };
        FakeStore     store{ received, sizeof received, 0 };

        void*    self    = reinterpret_cast<void*>(0x1000);
        void*    message = &store;
        uint32_t when    = 7;
        int32_t  unused  = 0;
        int32_t  answer  = -1;

        bool taken = wxl::seam::NetClient::ProcessMessage::Gate(&answer, &self, &when, &message,
                                                                &unused);
        Check(!taken, "a message nobody stops lets the client's own dispatch run");
        Check(incoming.calls == 1 && incoming.opcode == 0x1234u,
              "the opcode is the uint16 the server wrote");
        Check(incoming.size == 3u && incoming.first == 0xAA,
              "and the body is what follows the opcode");
        Check(store.read == 0u, "reading the message left the store's cursor where it was");

        incoming.stop = true;
        taken = wxl::seam::NetClient::ProcessMessage::Gate(&answer, &self, &when, &message,
                                                           &unused);
        Check(taken && answer == 0,
              "a handler that suppresses it takes the call, and the client answers 0");

        // An outgoing message: a uint32 opcode, then two bytes of body.
        const uint8_t sent[] = { 0x78, 0x56, 0x00, 0x00, 0x11, 0x22 };
        FakeStore     outStore{ sent, sizeof sent, 0 };
        message = &outStore;

        Check(!wxl::seam::NetClient::Send::Gate(&self, &message), "a message nobody stops is sent");
        Check(outgoing.calls == 1 && outgoing.opcode == 0x5678u,
              "the opcode is the uint32 the client wrote");
        Check(outgoing.size == 2u && outgoing.first == 0x11, "and the body is what follows it");

        outgoing.stop = true;
        Check(wxl::seam::NetClient::Send::Gate(&self, &message),
              "a handler that suppresses it stops the send");

        // A store that has been reset carries a cursor past its end: there is no message in it.
        outgoing.stop = false;
        FakeStore reset{ sent, 0, 0xFFFFFFFFu };
        message = &reset;
        const int before = outgoing.calls;
        Check(!wxl::seam::NetClient::Send::Gate(&self, &message),
              "a reset store is not a message, and the call goes through untouched");

        FakeStore stub{ sent, 2, 0 };
        message = &stub;
        Check(!wxl::seam::NetClient::Send::Gate(&self, &message),
              "neither is a store too short to name an opcode");
        Check(outgoing.calls == before, "and neither of those raised anything");

        g_host->Events().Unsubscribe(in);
        g_host->Events().Unsubscribe(out);
    }

    // --- Input: a gate that can answer a window message in the client's place -----------------

    bool g_swallow = false;
    int  g_messages = 0;

    void WXL_CALL OnInput(void*, const void* args)
    {
        const wxl_input_args& input = *static_cast<const wxl_input_args*>(args);
        ++g_messages;
        if (g_swallow && input.handled) *input.handled = WXL_TRUE;
    }

    void CheckInput()
    {
        const wxl_subscription sub =
            g_host->Events().Subscribe(WXL_EV_Input, &OnInput, nullptr, 0);

        void*     window  = reinterpret_cast<void*>(0x2000);
        uint32_t  message = 0x0100; // WM_KEYDOWN
        uintptr_t wparam  = 0x41;
        intptr_t  lparam  = 1;
        intptr_t  answer  = -1;

        g_swallow = false;
        Check(!wxl::seam::Input::WindowProc::Gate(&answer, &window, &message, &wparam, &lparam),
              "a message nobody claims reaches the client's own window procedure");
        Check(g_messages == 1, "and the handler saw it once");

        g_swallow = true;
        Check(wxl::seam::Input::WindowProc::Gate(&answer, &window, &message, &wparam, &lparam),
              "a handler that writes handled takes the call away from the client");
        Check(answer == 0, "and the window procedure answers 0, as one that handled a message does");

        g_host->Events().Unsubscribe(sub);
    }

    // --- wxl.data: a client database served from a mod file instead of the client's ----------

    void CheckDataService()
    {
        g_files.clear();
        g_files.push_back(FakeFile{"Mods\\mymod\\Spell.dbc", "WDBC-mine", 0, false});
        wxl::core::services::data::Clear();

        const wxl_data_api& api = wxl::core::services::DataApi();
        const wxl_str mine = Str("Mods\\mymod\\Spell.dbc");

        Check(api.serve_table(Str("Spell.dbc"), Str("Mods\\mymod\\Nope.dbc")) == WXL_E_NOT_FOUND,
              "a table cannot be served from a file the client cannot read");
        Check(api.serve_table(Str("Spell.dbc"), mine) == WXL_OK, "and can be from one it can");
        Check(api.serve_table(Str("DBFilesClient\\Spell.dbc"), mine) == WXL_E_FAILED,
              "the same table under its full name is the same table, and is already taken");

        // What the gate answers for the path the client is about to open, written where the
        // seam keeps it: the service hands over a copy, never a pointer into its own tables.
        char        redirect[512] = {};
        const char* full = "DBFilesClient\\Spell.dbc";
        Check(wxl::core::services::data::ServedPath(full, strlen(full), redirect, sizeof redirect)
                  && strcmp(redirect, "Mods\\mymod\\Spell.dbc") == 0,
              "so the client opens the mod's file instead of its own");

        const char* other = "DBFilesClient\\Item.dbc";
        Check(!wxl::core::services::data::ServedPath(other, strlen(other), redirect,
                                                    sizeof redirect),
              "a database nobody serves is opened as the client asked");
        const char* texture = "World\\Maps\\Azeroth.wdt";
        Check(!wxl::core::services::data::ServedPath(texture, strlen(texture), redirect,
                                                    sizeof redirect),
              "and a file that is not a database is not even looked up");
        const char* upper = "DBFilesClient\\CHARTITLES.DBC";
        Check(!wxl::core::services::data::ServedPath(upper, strlen(upper), redirect,
                                                    sizeof redirect)
                  && api.is_loaded(Str("CharTitles.dbc")) == WXL_TRUE,
              "a database named in capitals is the same database, suffix and all");

        Check(api.is_loaded(Str("Spell.dbc")) == WXL_TRUE
                  && api.is_loaded(Str("Talent.dbc")) == WXL_FALSE,
              "a table the client has read is known to have been read");
        Check(api.serve_table(Str("Item.dbc"), mine) == WXL_OK,
              "serving one the client read already is allowed");
        Check(api.restore_table(Str("Item.dbc")) == WXL_OK
                  && api.served_from(Str("Item.dbc")).size == 0,
              "and giving a table back leaves the client's own file");

        wxl::core::services::data::Clear();
        g_files.clear();
    }

    // --- Sound: a gate that can keep a sound from starting -------------------------------------

    bool g_hushSound = false;
    int  g_sounds    = 0;
    uint32_t g_lastKit = 0;

    void WXL_CALL OnSound(void*, const void* args)
    {
        const wxl_sound_args& sound = *static_cast<const wxl_sound_args*>(args);
        ++g_sounds;
        g_lastKit = sound.sound_id;
        if (g_hushSound && sound.suppressed) *sound.suppressed = WXL_TRUE;
    }

    void CheckSound()
    {
        const wxl_subscription sub =
            g_host->Events().Subscribe(WXL_EV_SoundPlay, &OnSound, nullptr, 0);

        int32_t     kit        = 1234;
        const void* position   = nullptr;
        void*       object     = nullptr;
        void*       properties = nullptr;
        uint32_t    advanced   = 0;
        uint32_t*   soundId    = nullptr;
        int32_t     distance   = 1;
        int32_t     keep       = 0;
        int32_t     answer     = -1;

        g_hushSound = false;
        Check(!wxl::seam::Sound::PlaySoundKit::Gate(&answer, &kit, &position, &object,
                                                     &properties, &advanced, &soundId,
                                                     &distance, &keep),
              "a sound nobody stops is played by the client");
        Check(g_sounds == 1 && g_lastKit == 1234u,
              "and the handler is told which kit, whether it was named by id or by path");

        g_hushSound = true;
        Check(wxl::seam::Sound::PlaySoundKit::Gate(&answer, &kit, &position, &object,
                                                    &properties, &advanced, &soundId,
                                                    &distance, &keep),
              "a handler that suppresses it takes the call");
        Check(answer == 17, "and the client is answered as it answers itself when sound is off");

        g_host->Events().Unsubscribe(sub);
    }

    // --- Storage: a gate that changes what the client is about to open ------------------------

    const char* g_answerWith = nullptr;

    void WXL_CALL OnFileOpen(void*, const void* args)
    {
        const wxl_file_open_args& open = *static_cast<const wxl_file_open_args*>(args);
        if (!g_answerWith || !open.redirect || !open.redirect_capacity) return;
        const size_t length = strlen(g_answerWith);
        if (length + 1 > open.redirect_capacity) return;
        memcpy(open.redirect, g_answerWith, length + 1);
    }

    void CheckStorage()
    {
        const wxl_subscription sub =
            g_host->Events().Subscribe(WXL_EV_FileOpen, &OnFileOpen, nullptr, 0);

        void*       archive = nullptr;
        const char* asked   = "World\\Maps\\Azeroth\\Azeroth.wdt";
        const char* name    = asked;
        uint32_t    flags   = 0;
        void**      file    = nullptr;
        int32_t     answer  = -1;

        g_answerWith = nullptr;
        Check(!wxl::seam::Storage::OpenFile::Gate(&answer, &archive, &name, &flags, &file),
              "a file nobody claims is opened by the client, as asked");
        Check(name == asked, "and the name it was handed is untouched");

        g_answerWith = "Mods\\example\\Azeroth.wdt";
        Check(!wxl::seam::Storage::OpenFile::Gate(&answer, &archive, &name, &flags, &file),
              "a redirected file is still opened by the client: the gate does not take the call");
        Check(name != asked && strcmp(name, g_answerWith) == 0,
              "but what it opens is the path the handler wrote");

        // The redirected path has to outlive the gate: the client opens it afterwards.
        const char* kept = name;
        g_answerWith     = nullptr;
        name             = asked;
        wxl::seam::Storage::OpenFile::Gate(&answer, &archive, &name, &flags, &file);
        Check(kept[0] == 0,
              "a redirect lives in one buffer per thread, so it lasts until that thread opens "
              "the next file and no longer");

        g_host->Events().Unsubscribe(sub);
    }

    /// Client.Destroy: the last thing a mod is told, and the point at which what a mod left
    /// jumping into its own code is taken away before that code is freed.
    // --- Render: the slots of a frame --------------------------------------------------------
    //
    // FrameBegin and FrameEnd come from two seams, and the client presents more often than it paints
    // a frame: its loading screen presents frames of its own, and a paint with nothing to draw into
    // presents anyway. What a mod is promised is pairs, in the client's order, and nothing outside
    // a frame. Each way the client can break that is driven here.

    void CheckRender()
    {
        wxl::core::frame::Reset();
        int top   = 0; // CSimpleTop's `this`; the seam does not read it
        int world = 0; // the world frame the world pass is handed

        auto paint = [] { wxl::seam::Scrn::OnPaint::Before(nullptr, nullptr); };
        auto present = []
        {
            wxl::seam::Gx::ScenePresent::Before();
        };

        g_gxContext = false;
        g_received.clear();
        paint();
        present();
        Check(g_received.empty(), "a paint with no device context opens no frame, and ends none");

        g_gxContext = true;
        g_gxVisible = false;
        g_received.clear();
        paint();
        present();
        Check(g_received.empty(), "nor does one whose window is minimised");
        g_gxVisible = true;

        g_received.clear();
        present();
        Check(g_received.empty(), "a present no paint opened a frame for, as the loading screen's, "
                                  "raises nothing");

        g_received.clear();
        wxl::seam::WorldFrame::RenderWorld::Before(&world);
        wxl::seam::WorldFrame::RenderWorld::After(&world);
        wxl::seam::SimpleTop::OnLayerRender::After(&top);
        Check(g_received.empty(), "the world pass and the interface's end raise nothing outside a frame");

        // A frame in the client's own order: the paint, the interface drawing with the world inside
        // it, the present. A frame announces no stage: by the time the client paints one, the
        // script context already exists, so the graphics stage is the device's to announce.
        wxl::core::stages::Restart();
        wxl::core::stages::Reach(WXL_STAGE_STORAGE);
        g_received.clear();
        paint();
        Check(wxl::core::stages::Reached() == WXL_STAGE_STORAGE, "a frame announces no stage");
        wxl::seam::WorldFrame::RenderWorld::Before(&world);
        wxl::seam::WorldFrame::RenderWorld::After(&world);
        wxl::seam::SimpleTop::OnLayerRender::After(&top);
        present();

        const wxl_event order[] = {WXL_EV_FrameBegin, WXL_EV_WorldRenderBegin, WXL_EV_WorldRenderEnd,
                                   WXL_EV_UiRenderEnd, WXL_EV_FrameEnd};
        bool inOrder = g_received.size() == 5;
        for (size_t i = 0; inOrder && i < 5; ++i) inOrder = g_received[i].event == order[i];
        Check(inOrder, "a frame raises FrameBegin, the world pass, the interface's end and "
                       "FrameEnd, in that order");

        uint64_t first = 0;
        if (inOrder)
        {
            first     = g_received[0].As<wxl_frame_args>().index;
            bool same = first != 0;
            for (const Received& received : g_received)
                same = same && received.As<wxl_frame_args>().index == first;
            Check(same, "and every slot of it carries that frame's index");
        }

        g_received.clear();
        present();
        Check(g_received.empty(), "a frame ends once: a second present after it raises nothing");

        g_received.clear();
        paint();
        paint();
        Check(Count(WXL_EV_FrameBegin) == 1, "a paint reached again with its frame still open "
                                             "opens no second one");
        present();
        Check(Count(WXL_EV_FrameEnd) == 1, "and that frame closes once");
        const Received* next = Only(WXL_EV_FrameBegin);
        Check(next && next->As<wxl_frame_args>().index == first + 1, "the next frame counts up by one");

        // The device going and coming back carries the size it renders at.
        g_gxWidth  = 1280;
        g_gxHeight = 720;
        g_received.clear();
        wxl::seam::Gx::ReleaseD3dResources::Before(&top, 0);
        const Received* lost = Only(WXL_EV_SwapchainLost);
        Check(lost && lost->As<wxl_swapchain_args>().width == 1280
                  && lost->As<wxl_swapchain_args>().height == 720,
              "SwapchainLost carries the size that is about to go away");

        g_gxWidth  = 2560;
        g_gxHeight = 1440;
        g_received.clear();
        wxl::seam::Gx::NotifyOnDeviceRestored::Before(&top);
        const Received* back = Only(WXL_EV_SwapchainRestored);
        Check(back && back->As<wxl_swapchain_args>().width == 2560
                  && back->As<wxl_swapchain_args>().height == 1440,
              "and SwapchainRestored the size it came back at");

        // With no device at all there is no size to report, and none is made up.
        g_gxDevice = false;
        g_received.clear();
        wxl::seam::Gx::ReleaseD3dResources::Before(&top, 0);
        const Received* gone = Only(WXL_EV_SwapchainLost);
        Check(gone && gone->As<wxl_swapchain_args>().width == 0
                  && gone->As<wxl_swapchain_args>().height == 0,
              "a device torn down reports no size rather than an invented one");
        g_gxDevice = true;
        g_gxWidth  = 1920;
        g_gxHeight = 1080;
        wxl::core::frame::Reset();

        // The graphics stage belongs to the device being made, between storage and script, which
        // is the order InitializeGlobal and InitializeClient bring them up in.
        wxl::core::stages::Restart();
        wxl::core::stages::Reach(WXL_STAGE_STORAGE);
        g_gxDevice = false;
        wxl::seam::ConsoleDevice::Initialize::After("World of Warcraft", true, true);
        Check(wxl::core::stages::Reached() == WXL_STAGE_STORAGE,
              "a device bring-up that left no device announces no graphics stage");
        g_gxDevice = true;
        wxl::seam::ConsoleDevice::Initialize::After("World of Warcraft", true, true);
        Check(wxl::core::stages::Reached() == WXL_STAGE_GRAPHICS,
              "one that left a device announces the graphics stage");
        wxl::core::stages::Reach(WXL_STAGE_SCRIPT);
        Check(wxl::core::stages::Reached() == WXL_STAGE_SCRIPT, "and script still follows it");
    }

    void CheckClientDestroy()
    {
        namespace stages = wxl::core::stages;
        stages::Restart();
        stages::Reach(WXL_STAGE_READY);
        g_released = 0;

        wxl::seam::Client::Destroy::Before();
        Check(stages::Reached() == WXL_STAGE_SHUTDOWN,
              "Client.Destroy announces the shutdown stage");
        Check(g_released == 1,
              "and takes back every detour a mod left behind, before its binary is freed");

        stages::Restart();
    }

    // --- NetClient: the connection coming up and going away ------------------------------------

    void CheckNetConnection()
    {
        g_received.clear();
        wxl::seam::NetClient::HandleConnect::After(1, nullptr);
        Check(Only(WXL_EV_Connected) != nullptr,
              "it is raised once the client has moved itself into the connected state");

        g_received.clear();
        wxl::seam::NetClient::HandleDisconnect::Before(nullptr);
        Check(Only(WXL_EV_Disconnected) != nullptr,
              "Disconnected is raised before the client tears the connection down");
    }

    // --- wxl.storage: the client's files, as a mod reads them ---------------------------------

    void CheckStorageService()
    {
        g_files.clear();
        g_files.push_back(FakeFile{"World\\Maps\\Azeroth\\Azeroth.wdt", "MVER....hello", 0, false});
        g_opens  = 0;
        g_closes = 0;

        const wxl_storage_api& api = wxl::core::services::StorageApi();
        const wxl_str          path{g_files[0].name.c_str(),
                                    static_cast<uint32_t>(g_files[0].name.size())};
        const wxl_str          missing = Str("World\\Maps\\Nowhere.wdt");

        Check(api.exists(path) == WXL_TRUE && api.exists(missing) == WXL_FALSE,
              "wxl.storage answers for a name the client has and one it does not");

        uint64_t size = 0;
        Check(api.size(path, &size) == WXL_OK && size == g_files[0].bytes.size(),
              "the length of a file is its length");
        Check(api.size(missing, &size) == WXL_E_NOT_FOUND,
              "and a file that is not there is not found, rather than empty");

        // A buffer too small answers what it would have taken, which is how the facade sizes.
        char     small[4] = {};
        uint64_t needed   = 0;
        Check(api.read(path, small, sizeof small, &needed) == WXL_E_INVALID
                  && needed == g_files[0].bytes.size(),
              "a buffer too small is refused, and says what would have fitted");

        std::vector<char> whole(static_cast<size_t>(size));
        uint64_t          read = 0;
        Check(api.read(path, whole.data(), whole.size(), &read) == WXL_OK && read == size
                  && std::string(whole.begin(), whole.end()) == g_files[0].bytes,
              "and a buffer the right size gets the whole file");

        // Streaming: a handle, a read at an offset, a close.
        const wxl_handle file = api.open(path);
        Check(file != WXL_HANDLE_NONE, "a file opens");

        char     part[5] = {};
        uint64_t got     = 0;
        Check(api.read_at(file, 8, part, 5, &got) == WXL_OK && got == 5
                  && std::string(part, 5) == "hello",
              "and reads where it is asked to, not where it last was");

        api.close(file);
        Check(g_closes == g_opens, "a closed file is closed on the client's side too");
        Check(api.read_at(file, 0, part, 1, &got) == WXL_E_NOT_FOUND,
              "a handle kept past its close answers gone, not whatever took its place");

        // What a mod leaves open is closed for it.
        const wxl_handle leaked = api.open(path);
        Check(leaked != WXL_HANDLE_NONE, "a mod opens a file and forgets it");
        wxl::core::services::storage::CloseEverything();
        Check(g_closes == g_opens, "and the runtime closes it on the way out");
        Check(api.read_at(leaked, 0, part, 1, &got) == WXL_E_NOT_FOUND,
              "its handle stops resolving with it");

        g_files.clear();
    }

#if defined(_M_IX86)
    // --- The generated detour, on the architecture it is generated for --------------------------

    /// A client-shaped class: a non-static member, `this` in ECX, two stack arguments, a result.
    struct Thing
    {
        int32_t base = 100;
        int32_t __thiscall Add(int32_t a, int32_t b) { return base + a + b; }
    };

    int32_t g_shimBefore = 0;
    int32_t g_shimAfter  = 0;
    void*   g_shimSelf   = nullptr;

    void WXL_CALL ShimBefore(void* self, int32_t a, int32_t b)
    {
        g_shimSelf   = self;
        g_shimBefore = a * 10 + b;
    }

    void WXL_CALL ShimAfter(int32_t result, void* self, int32_t a, int32_t b)
    {
        g_shimAfter = self == g_shimSelf ? result + a + b : -1;
    }

    using ShimSeam = wxl::core::seams::DetourOf<wxl::seam::detail::conv_thiscall,
                                                int32_t(void*, int32_t, int32_t)>;
    using ShimLink = ShimSeam::Link<&ShimBefore, &ShimAfter>;

    /// Calls the generated __fastcall detour the way the hook engine's caller would: as the
    /// __thiscall it stands in for, so a wrong shim corrupts the stack here rather than in a
    /// running client.
    void CheckThiscallShim()
    {
        Thing thing;
        auto  member        = &Thing::Add;
        ShimLink::original  = reinterpret_cast<ShimSeam::Fn>(*reinterpret_cast<void**>(&member));

        using AsThiscall = int32_t(__thiscall*)(Thing*, int32_t, int32_t);
        const AsThiscall detour = reinterpret_cast<AsThiscall>(&ShimLink::Detour);
        const int32_t result = detour(&thing, 5, 7);

        Check(result == 112, "the thiscall shim returns the original's result (100 + 5 + 7)");
        Check(g_shimSelf == &thing && g_shimBefore == 57, "Before received `this` and the arguments");
        Check(g_shimAfter == 112 + 5 + 7, "After received the result first, then `this` and the arguments");
    }

    using ShimBeforeLink = ShimSeam::Link<&ShimBefore, nullptr>;
    using ShimAfterLink  = ShimSeam::Link<nullptr, &ShimAfter>;

    /// The detours of the one-sided shapes: the original runs, and only the declared side does.
    void CheckOneSidedShim()
    {
        Thing thing;
        auto  member = &Thing::Add;
        using AsThiscall = int32_t(__thiscall*)(Thing*, int32_t, int32_t);

        ShimBeforeLink::original = reinterpret_cast<ShimSeam::Fn>(*reinterpret_cast<void**>(&member));
        g_shimSelf = nullptr, g_shimBefore = 0, g_shimAfter = 0;
        int32_t result = reinterpret_cast<AsThiscall>(&ShimBeforeLink::Detour)(&thing, 5, 7);
        Check(result == 112 && g_shimBefore == 57 && g_shimAfter == 0,
              "a Before-only detour runs Before and the original, and no After");

        ShimAfterLink::original = reinterpret_cast<ShimSeam::Fn>(*reinterpret_cast<void**>(&member));
        g_shimSelf = &thing, g_shimBefore = 0, g_shimAfter = 0;
        result = reinterpret_cast<AsThiscall>(&ShimAfterLink::Detour)(&thing, 5, 7);
        Check(result == 112 && g_shimBefore == 0 && g_shimAfter == 112 + 5 + 7,
              "an After-only detour runs the original and After, and no Before");
    }

    int32_t g_gateCalls = 0;
    void*   g_gateSelf  = nullptr;
    bool    g_gateTakes = false;

    bool WXL_CALL ShimGate(int32_t* result, void** self, int32_t* a, int32_t* b)
    {
        ++g_gateCalls;
        g_gateSelf = *self;
        *a         = *a + 1; // what the original is called with, if it is called at all
        if (!g_gateTakes) return false;
        *result = *a * 1000 + *b;
        return true;
    }

    using ShimGateSeam = wxl::core::seams::GateOf<wxl::seam::detail::conv_thiscall,
                                                  int32_t(void*, int32_t, int32_t)>;
    using ShimGateLink = ShimGateSeam::Link<&ShimGate>;

    /// Drives the generated gate as the hook engine's caller would, both ways: once letting the
    /// original run with a rewritten argument, once taking the call away from it.
    void CheckThiscallGate()
    {
        Thing thing;
        auto  member = &Thing::Add;
        ShimGateLink::original =
            reinterpret_cast<ShimGateSeam::Fn>(*reinterpret_cast<void**>(&member));

        using AsThiscall = int32_t(__thiscall*)(Thing*, int32_t, int32_t);
        const AsThiscall detour = reinterpret_cast<AsThiscall>(&ShimGateLink::Detour);

        g_gateTakes    = false;
        int32_t result = detour(&thing, 5, 7);
        Check(result == 100 + 6 + 7,
              "a gate that lets the call through rewrites what the original is given");
        Check(g_gateSelf == &thing && g_gateCalls == 1, "and it was handed the object once");

        g_gateTakes = true;
        result      = detour(&thing, 5, 7);
        Check(result == 6 * 1000 + 7, "a gate that takes the call answers in the original's place");
        Check(g_gateCalls == 2, "and the original never ran");
    }

    // --- The register thunks, against stand-ins with the client's conventions -------------------

    // What a stand-in original found in its registers and on its stack.
    uint32_t g_seenEdi = 0, g_seenEsi = 0, g_seenEax = 0, g_seenEcx = 0;
    uint32_t g_seenArg0 = 0, g_seenArg1 = 0;

    // What the caller's callee-saved registers held once the thunk returned.
    uint32_t g_keptEbx = 0, g_keptEsi = 0, g_keptEdi = 0;

    __declspec(naked) void FakeDispatchUpdate()
    {
        __asm {
            mov  dword ptr [g_seenEdi], edi
            mov  eax, dword ptr [esp+4]
            mov  dword ptr [g_seenArg0], eax
            mov  eax, dword ptr [esp+8]
            mov  dword ptr [g_seenArg1], eax
            ret
        }
    }

    __declspec(naked) void FakeInitObject()
    {
        __asm {
            mov  dword ptr [g_seenEsi], esi
            mov  dword ptr [g_seenEax], eax
            mov  dword ptr [g_seenEcx], ecx
            ret
        }
    }

    __declspec(naked) void FakeObjDelete()
    {
        __asm {
            mov  dword ptr [g_seenEax], eax
            ret
        }
    }

    __declspec(naked) void FakeDestroyRemovedObject()
    {
        __asm {
            mov  dword ptr [g_seenEsi], esi
            ret
        }
    }

    /// Calls a thunk the way IEvtDispatchUpdate's caller does: context in EDI, two stack slots
    /// the caller cleans.
    void CallWithEdi(void* thunk, void* context, uint32_t arg0, uint32_t arg1)
    {
        __asm {
            mov  ebx, 0B0B0B0Bh
            mov  esi, 05050505h
            mov  edi, context
            push arg1
            push arg0
            call thunk
            add  esp, 8
            mov  dword ptr [g_keptEbx], ebx
            mov  dword ptr [g_keptEsi], esi
            mov  dword ptr [g_keptEdi], edi
        }
    }

    /// Calls a thunk the way InitObject's caller does: ESI, EAX and ECX, nothing pushed.
    void CallWithEsiEaxEcx(void* thunk, void* esiValue, uint32_t eaxValue, uint32_t ecxValue)
    {
        __asm {
            mov  ebx, 0B0B0B0Bh
            mov  edi, 0D0D0D0Dh
            mov  esi, esiValue
            mov  eax, eaxValue
            mov  ecx, ecxValue
            call thunk
            mov  dword ptr [g_keptEbx], ebx
            mov  dword ptr [g_keptEsi], esi
            mov  dword ptr [g_keptEdi], edi
        }
    }

    /// Calls a thunk the way ObjDelete's callers do: the object in EAX, nothing pushed.
    void CallWithEax(void* thunk, void* eaxValue)
    {
        __asm {
            mov  ebx, 0B0B0B0Bh
            mov  esi, 05050505h
            mov  edi, 0D0D0D0Dh
            mov  eax, eaxValue
            call thunk
            mov  dword ptr [g_keptEbx], ebx
            mov  dword ptr [g_keptEsi], esi
            mov  dword ptr [g_keptEdi], edi
        }
    }

    /// Every regparm row has a thunk, and each thunk hands the original its registers back.
    void CheckRegparmThunks()
    {
        namespace regparm = wxl::core::seams::regparm;

        for (const wxl::core::seams::Seam& seam : wxl::core::seams::Table())
        {
            if (strcmp(seam.convention, "regparm") != 0) continue;
            char what[160];
            snprintf(what, sizeof what, "%s takes its arguments in registers and has a thunk", seam.name);
            Check(regparm::Find(seam.name) != nullptr, what);
        }
        Check(regparm::Find("World.Preload") == nullptr, "a seam with a stack convention has none");

        const regparm::Row* update  = regparm::Find("Event.DispatchUpdate");
        const regparm::Row* init    = regparm::Find("ObjectMgr.InitObject");
        const regparm::Row* remove  = regparm::Find("ObjectMgr.ObjDelete");
        const regparm::Row* destroy = regparm::Find("ObjectMgr.DestroyRemovedObject");
        if (!update || !init || !remove || !destroy) return;

        *update->original  = reinterpret_cast<void*>(&FakeDispatchUpdate);
        *init->original    = reinterpret_cast<void*>(&FakeInitObject);
        *remove->original  = reinterpret_cast<void*>(&FakeObjDelete);
        *destroy->original = reinterpret_cast<void*>(&FakeDestroyRemovedObject);

        int         context = 0;
        const float elapsed = 0.25f;
        uint32_t    elapsedBits;
        memcpy(&elapsedBits, &elapsed, sizeof elapsedBits);

        g_received.clear();
        CallWithEdi(update->detour, &context, 4242u, elapsedBits);
        Check(g_seenEdi == reinterpret_cast<uint32_t>(&context),
              "Event.DispatchUpdate's original finds the context in EDI");
        Check(g_seenArg0 == 4242u && g_seenArg1 == elapsedBits, "and its two stack arguments");
        const Received* tick = Only(WXL_EV_Update);
        Check(tick && tick->As<wxl_update_args>().delta == elapsed,
              "Before read the arguments the thunk was handed");
        Check(g_keptEbx == 0x0B0B0B0Bu && g_keptEsi == 0x05050505u &&
                  g_keptEdi == reinterpret_cast<uint32_t>(&context),
              "and the caller gets EBX, ESI and EDI back");

        FakeObject spider;
        spider.guid   = 0xF130000000000777ull;
        spider.typeId = WXL_OBJECT_UNIT;

        g_received.clear();
        CallWithEsiEaxEcx(init->detour, &spider, 0x1234u, 77u);
        Check(g_seenEsi == reinterpret_cast<uint32_t>(&spider) && g_seenEax == 0x1234u &&
                  g_seenEcx == 77u,
              "ObjectMgr.InitObject's original finds the object in ESI, the block in EAX, the time in ECX");
        const Received* created = Only(WXL_EV_ObjectCreate);
        Check(created && created->As<wxl_object_args>().guid == spider.guid,
              "After was handed the object from ESI");
        Check(g_keptEbx == 0x0B0B0B0Bu && g_keptEdi == 0x0D0D0D0Du &&
                  g_keptEsi == reinterpret_cast<uint32_t>(&spider),
              "and the caller gets EBX, ESI and EDI back");

        g_received.clear();
        CallWithEax(remove->detour, &spider);
        Check(g_seenEax == reinterpret_cast<uint32_t>(&spider),
              "ObjectMgr.ObjDelete's original finds the object in EAX");
        Check(Only(WXL_EV_ObjectDestroy) != nullptr, "Before was handed the object from EAX");
        Check(g_keptEbx == 0x0B0B0B0Bu && g_keptEsi == 0x05050505u && g_keptEdi == 0x0D0D0D0Du,
              "and the caller gets EBX, ESI and EDI back");

        FakeObject moth;
        CallWithEsiEaxEcx(destroy->detour, &moth, 0u, 0u);
        Check(g_seenEsi == reinterpret_cast<uint32_t>(&moth),
              "ObjectMgr.DestroyRemovedObject's original finds the object in ESI");

        *update->original  = nullptr;
        *init->original    = nullptr;
        *remove->original  = nullptr;
        *destroy->original = nullptr;
        wxl::core::stages::Restart();
    }
#endif
}


// --- A measurement, not a check ---------------------------------------------------------------
// Two paths carry work at a rate nothing else here does: the file-open gate, which the client
// reaches once per file it opens, and the event bus, which every seam goes through. These are
// not frame times -- there is no client and no archive behind them -- but they are the real code
// on the real path, so a change to either shows up here before anyone notices it in game.

void WXL_CALL BenchHandler(void*, const void*) {}

void Bench()
{
    using Clock = std::chrono::steady_clock;

    const char* texture = "World\\Expansion01\\Doodads\\Generic\\BloodElf\\Banner01.blp";
    const char* table   = "DBFilesClient\\Spell.dbc";
    char        out[512];

    auto time = [](const char* what, int runs, auto&& body) {
        body();                                 // once, so nothing measured is a cold page
        const Clock::time_point start = Clock::now();
        for (int i = 0; i < runs; ++i) body();
        const double ns =
            std::chrono::duration<double, std::nano>(Clock::now() - start).count() / runs;
        printf("  %-46s %8.1f ns/call\n", what, ns);
    };

    printf("\nbench (%s):\n", sizeof(void*) == 8 ? "x64" : "x86");

    wxl::core::services::data::Clear();
    time("file-open gate, a path that is not a database", 200000, [&] {
        wxl::core::services::data::ServedPath(texture, strlen(texture), out, sizeof out);
    });
    time("file-open gate, a database nobody serves", 200000, [&] {
        wxl::core::services::data::ServedPath(table, strlen(table), out, sizeof out);
    });

    // A bus of its own: the harness's bus carries the recording handlers of the checks.
    wxl::host::EventBus bus({WXL_EV_Update}, nullptr);
    wxl_update_args     args{};
    args.delta = 0.016f;
    time("event bus, no subscriber", 200000, [&] { bus.Emit(WXL_EV_Update, &args); });

    bus.Subscribe(WXL_EV_Update, &BenchHandler, nullptr, 0);
    time("event bus, one subscriber", 200000, [&] { bus.Emit(WXL_EV_Update, &args); });
    bus.Subscribe(WXL_EV_Update, &BenchHandler, nullptr, 0);
    time("event bus, two subscribers", 200000, [&] { bus.Emit(WXL_EV_Update, &args); });

    wxl::core::services::data::Clear();
}

int main(int argc, char** argv)
{
    wxl::core::binding::Bind(FakeInit());

    wxl::host::HostConfig config;
    config.runtime_id   = "harness";
    config.runtime_name = "seam harness";
    config.events       = wxl::core::seams::RaisedEvents();
    config.services.push_back(wxl::host::PublishedService{
        WXL_SCRIPT_SERVICE, WXL_SCRIPT_SERVICE_VERSION, &wxl::core::services::ScriptApi()});
    g_host              = std::make_unique<wxl::host::ModHost>(std::move(config));

    printf("seam harness: %zu seam(s), %zu raisable event(s), %zu binding row(s) left unbound by the fake\n",
           wxl::core::seams::Table().size(), wxl::core::seams::RaisedEvents().size(),
           wxl::core::binding::Unbound().size());

    CheckStages();
    CheckInitRows();
    CheckEveryRaisedEventIsSubscribable();
    CheckEveryRowDeclaresItsAbi();
    CheckEventDispatchUpdate();
    CheckWorldPreload();
    CheckObjectMgr();
    CheckUnit();
    CheckPlayer();
    CheckGameUI();
    CheckMapArea();
    CheckFrameScript();
    CheckNetClient();
    CheckNetConnection();
    CheckClientDestroy();
    CheckInput();
    CheckStorage();
    CheckSound();
    CheckStorageService();
    CheckDataService();
    CheckRender();
#if defined(_M_IX86)
    CheckThiscallShim();
    CheckOneSidedShim();
    CheckThiscallGate();
    CheckRegparmThunks();
#endif

    printf("\n%d failure(s)\n", g_failures);
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], "--bench") == 0) Bench();

    g_host.reset();
    return g_failures ? 1 : 0;
}
