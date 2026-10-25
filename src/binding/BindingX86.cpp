// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The x86 realisation of the client binding: memory reads at measured offsets, calls at resolved addresses.

#include "binding/Binding.hpp"
#include "binding/BindingX86.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Image.hpp"

#include "wxl/events.h"
#include "wxl/core/gpu.hpp"
#include "wxl/core/storm.hpp"
#include "wxl/offsets/layout.hpp"
#include "wxl/offsets/site.hpp"
#include "wxl/services/script.h"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    DWORD g_mainThread = 0;

    bool IsClient() { return wxl::core::image::IsClient(); }

    bool OnMainThread()
    {
        if (GetCurrentThreadId() == g_mainThread) return true;
        WLOG_WARN(wxl::core::kTag, "a binding row that reads the object manager was called off "
                                   "the main thread; it answers absent");
        return false;
    }

    /// A value of T at a byte offset from a base, read without alignment assumptions.
    template <class T>
    T Read(const void* base, std::size_t offset)
    {
        T value;
        std::memcpy(&value, static_cast<const char*>(base) + offset, sizeof value);
        return value;
    }

    /// The block a pointer member points at, or null when the member is null.
    const void* Via(const void* object, std::size_t offset)
    {
        return object ? Read<const void*>(object, offset) : nullptr;
    }

    // The client's own getters, called as the __thiscall members they are: `this` in ECX, a dummy
    // EDX, the stack arguments after. A C3Vector return goes through the hidden pointer the
    // caller passes as the first stack argument, which the shim passes explicitly.
    using PositionFn = wxl_vec3*(__fastcall*)(const void* self, void* edx, wxl_vec3* out);
    using FacingFn   = float(__fastcall*)(const void* self, void* edx);
    using NameFn     = const char*(__fastcall*)(const void* self, void* edx);
    using PlayerFn   = void*(__cdecl*)();

    wxl_bool CallPosition(std::uintptr_t site, const void* object, wxl_vec3* out)
    {
        reinterpret_cast<PositionFn>(site)(object, nullptr, out);
        return WXL_TRUE;
    }

    wxl_bool CallFacing(std::uintptr_t site, const void* object, float* out)
    {
        *out = reinterpret_cast<FacingFn>(site)(object, nullptr);
        return WXL_TRUE;
    }

    const char* CallName(std::uintptr_t site, const void* object)
    {
        return reinterpret_cast<NameFn>(site)(object, nullptr);
    }

    constexpr std::size_t kCorpsePosition = WXL_FIELD("CGCorpse_C", "m_passenger") + WXL_FIELD("CPassenger", "m_position");

    // The client's Lua 5.1 and the FrameScript layer, called as the C functions they are. The
    // pseudo-indices and type codes are Lua 5.1's public API, the same in every build of it.
    constexpr int32_t kLuaGlobalsIndex = -10002;              // LUA_GLOBALSINDEX
    constexpr int32_t kLuaUpvalue1     = kLuaGlobalsIndex - 1; // lua_upvalueindex(1)
    constexpr int32_t kLuaTBoolean     = 1;
    constexpr int32_t kLuaTNumber      = 3;
    constexpr int32_t kLuaTString      = 4;

    using LuaGetTop        = int32_t(__cdecl*)(void* L);
    using LuaSetTop        = void(__cdecl*)(void* L, int32_t index);
    using LuaType          = int32_t(__cdecl*)(void* L, int32_t index);
    using LuaToNumber      = double(__cdecl*)(void* L, int32_t index);
    using LuaToLString     = const char*(__cdecl*)(void* L, int32_t index, std::size_t* length);
    using LuaToBoolean     = int32_t(__cdecl*)(void* L, int32_t index);
    using LuaPushNumber    = void(__cdecl*)(void* L, double value);
    using LuaPushLString   = void(__cdecl*)(void* L, const char* text, std::size_t length);
    using LuaPushBoolean   = void(__cdecl*)(void* L, int32_t value);
    using LuaPushNil       = void(__cdecl*)(void* L);
    using LuaPushCClosure  = void(__cdecl*)(void* L, wxl_script_native_fn fn, int32_t upvalues);
    using LuaGetField      = void(__cdecl*)(void* L, int32_t index, const char* key);
    using LuaSetField      = void(__cdecl*)(void* L, int32_t index, const char* key);
    using LuaError         = int32_t(__cdecl*)(void* L);
    using ScriptExecute    = void(__cdecl*)(const char* source, const char* name, const char* taint);
    using ScriptCreateEvts = void(__cdecl*)(const char** names, uint32_t count);
    using ScriptSignal     = void(__cdecl*)(uint32_t index, void* L, int32_t arg_count);

    // The client's file layer. STORMAPI is __stdcall, and every one of these has external
    // linkage, so the convention they are declared with is the convention they carry.
    using FileExists   = int32_t(__stdcall*)(const char* name);
    using FileOpen     = int32_t(__stdcall*)(const char* name, void** file);
    using FileSize     = uint32_t(__stdcall*)(void* file, uint32_t* high);
    using FileRead     = int32_t(__stdcall*)(void* file, void* buffer, uint32_t count,
                                             uint32_t* read, void* overlapped, uint32_t unused);
    using FileSeek     = int32_t(__stdcall*)(void* file, int32_t distance, int32_t* high,
                                             uint32_t method);
    using FileClose    = int32_t(__stdcall*)(void* file);
    using ArchiveOpen  = int32_t(__stdcall*)(const char* path, int32_t priority, uint32_t flags,
                                             void** archive);

    // The graphics device's queries. Each is a free function with external linkage and no
    // argument -- or one, for the size -- that reads g_theGxDevicePtr and asks the device, so
    // the convention they are declared with, cdecl, is the one they carry.
    using GxFlag       = int32_t(__cdecl*)();
    using GxWindowSize = void(__cdecl*)(float* rect); ///< a CRect: minY, minX, maxY, maxX

    /// The ARCHIVEREC* of every mounted archive, in the client's order.
    std::vector<void*> ArchiveRecords()
    {
        std::vector<void*> records;
        // System_MPQGlobals::GetArchiveList answers the std::set of mounted archives. Its layout
        // is MSVC 9's, as FindArchiveByName (0x00453FC0) and the set iterator's ++ (0x00429F90)
        // read it: the head node at +4; a node's left, parent and right at 0, 4 and 8, its value
        // (the ARCHIVEREC*) at 0xC, its is-nil flag at 0x11. The head is the nil node.
        using GetList = const uint8_t*(__cdecl*)();
        const uint8_t* set =
            reinterpret_cast<GetList>(WXL_SITE("System_MPQGlobals::GetArchiveList", 0))();
        if (!set) return records;
        const auto link = [](const uint8_t* node, size_t at) {
            return *reinterpret_cast<const uint8_t* const*>(node + at);
        };
        const auto nil = [](const uint8_t* node) { return node[0x11] != 0; };

        const uint8_t* head = link(set, 4);
        const uint8_t* node = link(head, 0);
        while (node && !nil(node) && records.size() < 4096)
        {
            records.push_back(*reinterpret_cast<void* const*>(node + 0xC));
            if (!nil(link(node, 8)))
            {
                node = link(node, 8);
                while (!nil(link(node, 0))) node = link(node, 0);
            }
            else
            {
                const uint8_t* parent = link(node, 4);
                while (!nil(parent) && node == link(parent, 8))
                {
                    node   = parent;
                    parent = link(parent, 4);
                }
                node = parent;
            }
        }
        return records;
    }

    /// The device the client draws with, or null before it is made and after it is torn down.
    const void* GxDevice() { return *reinterpret_cast<void* const*>(WXL_STATIC("g_theGxDevicePtr")); }

    LuaGetTop       LuaGetTopAt() { return reinterpret_cast<LuaGetTop>(WXL_SITE("lua_gettop", 0)); }
    LuaSetTop       LuaSetTopAt() { return reinterpret_cast<LuaSetTop>(WXL_SITE("lua_settop", 0)); }
    LuaType         LuaTypeAt() { return reinterpret_cast<LuaType>(WXL_SITE("lua_type", 0)); }
    LuaToNumber     LuaToNumberAt() { return reinterpret_cast<LuaToNumber>(WXL_SITE("lua_tonumber", 0)); }
    LuaToLString    LuaToLStringAt() { return reinterpret_cast<LuaToLString>(WXL_SITE("lua_tolstring", 0)); }
    LuaToBoolean    LuaToBooleanAt() { return reinterpret_cast<LuaToBoolean>(WXL_SITE("lua_toboolean", 0)); }
    LuaPushNumber   LuaPushNumberAt() { return reinterpret_cast<LuaPushNumber>(WXL_SITE("lua_pushnumber", 0)); }
    LuaPushLString  LuaPushLStringAt() { return reinterpret_cast<LuaPushLString>(WXL_SITE("lua_pushlstring", 0)); }
    LuaPushBoolean  LuaPushBooleanAt() { return reinterpret_cast<LuaPushBoolean>(WXL_SITE("lua_pushboolean", 0)); }
    LuaPushNil      LuaPushNilAt() { return reinterpret_cast<LuaPushNil>(WXL_SITE("lua_pushnil", 0)); }
    LuaPushCClosure LuaPushCClosureAt() { return reinterpret_cast<LuaPushCClosure>(WXL_SITE("lua_pushcclosure", 0)); }
    LuaGetField     LuaGetFieldAt() { return reinterpret_cast<LuaGetField>(WXL_SITE("lua_getfield", 0)); }
    LuaSetField     LuaSetFieldAt() { return reinterpret_cast<LuaSetField>(WXL_SITE("lua_setfield", 0)); }
    LuaError        LuaErrorAt() { return reinterpret_cast<LuaError>(WXL_SITE("lua_error", 0)); }

    // Two overloads carry each FrameScript name: the (source, name, taint) Execute and the
    // (index, L, argCount) SignalEvent are the ones wxl.script drives.
    ScriptExecute    ScriptExecuteAt() { return reinterpret_cast<ScriptExecute>(WXL_SITE_AT("FrameScript_Execute", 0x00819210, 0)); }
    ScriptCreateEvts ScriptCreateEventsAt() { return reinterpret_cast<ScriptCreateEvts>(WXL_SITE("FrameScript_CreateEvents", 0)); }
    ScriptSignal     ScriptSignalEventAt() { return reinterpret_cast<ScriptSignal>(WXL_SITE_AT("FrameScript_SignalEvent", 0x0081AA00, 0)); }

    uint32_t ValueType(int32_t luaType)
    {
        switch (luaType)
        {
        case kLuaTNumber: return WXL_VALUE_NUMBER;
        case kLuaTString: return WXL_VALUE_STRING;
        case kLuaTBoolean: return WXL_VALUE_BOOL;
        default: return WXL_VALUE_NIL;
        }
    }
}

namespace wxl::core::binding::x86
{
    void Prepare() { g_mainThread = GetCurrentThreadId(); }
}

namespace wxl::core::binding
{
    // --- the field rows, generated from the table --------------------------------------------

#define WXL_BINDING(name, ret, params, args, absent)

#define WXL_BINDING_FIELD(name, ret, Type, member, absent)                                       \
    ret name(const void* object)                                                                 \
    {                                                                                            \
        return object && IsClient() ? Read<ret>(object, WXL_FIELD(#Type, #member)) : (absent);   \
    }

#define WXL_BINDING_FIELD_VIA(name, ret, Type, via, ViaType, member, absent)                     \
    ret name(const void* object)                                                                 \
    {                                                                                            \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, #via)) : nullptr;          \
        return block ? Read<ret>(block, WXL_FIELD(#ViaType, #member)) : (absent);                \
    }

#define WXL_BINDING_FIELD_VIA_INDEX(name, ret, Type, via, ViaType, member)                       \
    wxl_bool name(const void* object, uint32_t index, ret* out)                                  \
    {                                                                                            \
        constexpr std::size_t count = WXL_MEMBER_SIZE(#ViaType, #member) / sizeof(ret);          \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, #via)) : nullptr;          \
        if (!block || !out || index >= count) return WXL_FALSE;                                  \
        *out = Read<ret>(block, WXL_FIELD(#ViaType, #member) + index * sizeof(ret));             \
        return WXL_TRUE;                                                                         \
    }

#define WXL_BINDING_FIELD_VIA_AT(name, ret, Type, via, ViaType, member, Elem, elem_member)       \
    wxl_bool name(const void* object, uint32_t index, ret* out)                                  \
    {                                                                                            \
        constexpr std::size_t stride = WXL_TYPE_SIZE(#Elem);                                     \
        constexpr std::size_t count  = WXL_MEMBER_SIZE(#ViaType, #member) / stride;              \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, #via)) : nullptr;          \
        if (!block || !out || index >= count) return WXL_FALSE;                                  \
        *out = Read<ret>(block, WXL_FIELD(#ViaType, #member) + index * stride +                  \
                                     WXL_FIELD(#Elem, #elem_member));                            \
        return WXL_TRUE;                                                                         \
    }

#include "wxl/core/binding.def"

#undef WXL_BINDING
#undef WXL_BINDING_FIELD
#undef WXL_BINDING_FIELD_VIA
#undef WXL_BINDING_FIELD_VIA_INDEX
#undef WXL_BINDING_FIELD_VIA_AT

    // --- the call rows, one body per WXL_BINDING row of the table ----------------------------

    int32_t map_id(void)
    {
        // The Map.cpp s_mapId: MinimapFrame.cpp and DayNight.cpp each have one of the same name.
        constexpr std::uintptr_t kMapId = WXL_STATIC_AT("s_mapId", 0x00ADFBC4);
        return IsClient() ? *reinterpret_cast<const int32_t*>(kMapId) : -1;
    }

    const void* local_player_object(void)
    {
        if (!IsClient() || !OnMainThread()) return nullptr;
        return reinterpret_cast<PlayerFn>(WXL_SITE("ClntObjMgrGetActivePlayerObj", 0))();
    }

    const void* object_by_guid(uint64_t guid)
    {
        // ClntObjMgrObjectPtr(WOWGUID, TypeMask, const char* file, int line), cdecl: the guid as
        // two dwords, the mask, and a source position it reads only to report a wrong kind.
        // TYPE_OBJECT (1) is every kind.
        using ObjectPtrFn = void*(__cdecl*)(uint64_t guid, uint32_t mask, const char* file, int32_t line);
        if (!IsClient() || !OnMainThread() || !guid) return nullptr;
        return reinterpret_cast<ObjectPtrFn>(WXL_SITE("ClntObjMgrObjectPtr", 0))(guid, 1u, "WarcraftXL", 0);
    }

    wxl_bool object_watch_type(uint32_t type, uint32_t offset, uint32_t size, wxl_core_mirror_fn handler,
                               void* param)
    {
        // ClntObjMgrSetTypeMirrorHandler(OBJECT_TYPE_ID, fieldOffset, fieldSize, MIRRORHANDLERFUNC,
        // param, HANDLER_PRIORITY, alwaysFire), cdecl: it reads [ebp+8]..[ebp+0x20] and returns
        // with a bare `ret` (0x004D5C13); CGUnit_C::ClientInitialize pushes seven dwords, calls,
        // and `add esp, 0x1c` (0x006E4648-0x006E465E). Priority 0 links the handler after those
        // already on the field; alwaysFire 0 calls it only when the bytes changed.
        using SetTypeFn = void(__cdecl*)(uint32_t type, uint32_t offset, uint32_t size, wxl_core_mirror_fn handler,
                                         void* param, int32_t priority, int32_t always);
        if (!IsClient() || !OnMainThread() || !handler) return WXL_FALSE;
        reinterpret_cast<SetTypeFn>(WXL_SITE("ClntObjMgrSetTypeMirrorHandler", 0))(type, offset, size, handler,
                                                                                    param, 0, 0);
        return WXL_TRUE;
    }

    wxl_bool object_watch_one(uint64_t guid, uint32_t type, uint32_t offset, uint32_t size,
                              wxl_core_mirror_fn handler, void* param)
    {
        // ClntObjMgrSetObjMirrorHandler(WOWGUID, OBJECT_TYPE_ID, fieldOffset, fieldSize,
        // MIRRORHANDLERFUNC, param, HANDLER_PRIORITY, alwaysFire), cdecl: the guid as two dwords
        // at [ebp+8] and [ebp+0xC], the rest to [ebp+0x28], a bare `ret` (0x004D5B33); the
        // explored-zones handler's setter at 0x00547120-0x00547143 pushes seven dwords and the
        // active player's guid (edx:eax), calls, and `add esp, 0x24`. It does nothing for a guid
        // the manager does not hold, which is asked first.
        using SetObjFn = void(__cdecl*)(uint64_t guid, uint32_t type, uint32_t offset, uint32_t size,
                                        wxl_core_mirror_fn handler, void* param, int32_t priority, int32_t always);
        if (!handler || !object_by_guid(guid)) return WXL_FALSE;
        reinterpret_cast<SetObjFn>(WXL_SITE("ClntObjMgrSetObjMirrorHandler", 0))(guid, type, offset, size, handler,
                                                                                  param, 0, 0);
        return WXL_TRUE;
    }

    wxl_bool object_unwatch_type(uint32_t type, uint32_t offset, wxl_core_mirror_fn handler, void* param)
    {
        // ClntObjMgrUnsetTypeMirrorHandler(OBJECT_TYPE_ID, fieldOffset, MIRRORHANDLERFUNC, param),
        // cdecl: it reads [ebp+8]..[ebp+0x14] and returns with a bare `ret` (0x004D5C70);
        // CGUnit_C::UnregisterMirrorHandlers pushes four dwords, calls, and `add esp, 0x10`
        // (0x00741FF6-0x00742008).
        using UnsetTypeFn = void(__cdecl*)(uint32_t type, uint32_t offset, wxl_core_mirror_fn handler, void* param);
        if (!IsClient() || !OnMainThread() || !handler) return WXL_FALSE;
        reinterpret_cast<UnsetTypeFn>(WXL_SITE("ClntObjMgrUnsetTypeMirrorHandler", 0))(type, offset, handler, param);
        return WXL_TRUE;
    }

    wxl_bool object_unwatch_one(uint64_t guid, uint32_t type, uint32_t offset, wxl_core_mirror_fn handler,
                                void* param)
    {
        // ClntObjMgrUnsetObjMirrorHandler(WOWGUID, OBJECT_TYPE_ID, fieldOffset, MIRRORHANDLERFUNC,
        // param), cdecl: the guid as two dwords at [ebp+8] and [ebp+0xC], the rest to [ebp+0x1C],
        // a bare `ret` (0x004D5B9B). It does nothing for a guid the manager does not hold, which is
        // asked first.
        using UnsetObjFn = void(__cdecl*)(uint64_t guid, uint32_t type, uint32_t offset, wxl_core_mirror_fn handler,
                                          void* param);
        if (!handler || !object_by_guid(guid)) return WXL_FALSE;
        reinterpret_cast<UnsetObjFn>(WXL_SITE("ClntObjMgrUnsetObjMirrorHandler", 0))(guid, type, offset, handler,
                                                                                      param);
        return WXL_TRUE;
    }

    uint64_t ui_target_guid(void)
    {
        return IsClient() ? *reinterpret_cast<const uint64_t*>(WXL_STATIC("CGGameUI::m_lockedTarget")) : 0;
    }

    int32_t ui_initialized(void)
    {
        return IsClient() ? *reinterpret_cast<const int32_t*>(WXL_STATIC("CGGameUI::m_initialized")) : 0;
    }

    uint8_t ui_logging_in(void)
    {
        return IsClient() ? *reinterpret_cast<const uint8_t*>(WXL_STATIC("CGGameUI::m_loggingIn")) : 0;
    }

    // --- glue.def: the login screens' state -------------------------------------------------

    const char* glue_current_screen(void)
    {
        // A char[64] static: its address is the string.
        return IsClient() ? reinterpret_cast<const char*>(WXL_STATIC("CGlueMgr::m_currentScreen")) : nullptr;
    }

    int32_t login_server_type(const void* login)
    {
        // Login::GetLoginServerType is the vtable's slot 50 (+0xC8): ClientServices::
        // GetLoginServerType calls it there (0x004D9FF2 `mov eax,[edx+0xc8]`), and the slot holds
        // 0x008E5250 (`xor eax,eax; ret`) in GruntLogin's vtable at 0x00A94928 and 0x008C8DE0
        // (`mov eax,1; ret`) in BattlenetLogin's at 0x00A94768.
        if (!IsClient() || !login) return -1;
        using ServerTypeFn = int32_t(__fastcall*)(const void* self, void* edx);
        const void* const* vtable = Read<const void* const*>(login, 0);
        return reinterpret_cast<ServerTypeFn>(const_cast<void*>(vtable[50]))(login, nullptr);
    }

    // --- chat.def: the lines the chat holds -------------------------------------------------

    const void* chat_pending_last(void)
    {
        // TSList::Tail as stpl.h writes it: the terminator's m_prevLink, that link's m_prevLink,
        // and its m_next unless the low bit marks the terminator. The list is the link offset
        // (+0), then the terminator: m_prevLink (+4), m_next (+8). QueueChatText reads the same
        // m_prevLink to append (0x00502D68 `mov ecx,[0x00AC7D40]`).
        if (!IsClient()) return nullptr;
        const void* list       = reinterpret_cast<const void*>(WXL_STATIC("CGChat::s_pendingChat"));
        const void* last       = Read<const void*>(list, 4);
        const void* beforeLast = last ? Read<const void*>(last, 0) : nullptr;
        if (!beforeLast) return nullptr;
        const std::uintptr_t next = Read<std::uintptr_t>(beforeLast, 4);
        return (next & 1u) ? nullptr : reinterpret_cast<const void*>(next);
    }

    // --- script.def: the client's Lua, called as the C functions they are ---------------------

    void* script_state(void)
    {
        // The FrameScript.cpp s_context: OsIME.cpp has one of the same name.
        constexpr std::uintptr_t kContext = WXL_STATIC_AT("s_context", 0x00D3F78C);
        return IsClient() ? *reinterpret_cast<void* const*>(kContext) : nullptr;
    }

    wxl_bool script_global_is_set(void* state, const char* name)
    {
        if (!IsClient() || !state || !name) return WXL_FALSE;
        LuaGetFieldAt()(state, kLuaGlobalsIndex, name);
        const int32_t type = LuaTypeAt()(state, -1);
        LuaSetTopAt()(state, -2);
        return type > 0 ? WXL_TRUE : WXL_FALSE;
    }

    void script_set_global_closure(void* state, const char* name, wxl_script_native_fn fn, uint32_t slot)
    {
        if (!IsClient() || !state || !name || !fn) return;
        LuaPushNumberAt()(state, static_cast<double>(slot));
        LuaPushCClosureAt()(state, fn, 1);
        LuaSetFieldAt()(state, kLuaGlobalsIndex, name);
    }

    void script_clear_global(void* state, const char* name)
    {
        if (!IsClient() || !state || !name) return;
        LuaPushNilAt()(state);
        LuaSetFieldAt()(state, kLuaGlobalsIndex, name);
    }

    uint32_t script_closure_slot(void* state)
    {
        if (!IsClient() || !state) return 0;
        return static_cast<uint32_t>(LuaToNumberAt()(state, kLuaUpvalue1));
    }

    int32_t script_top(void* state)
    {
        return IsClient() && state ? LuaGetTopAt()(state) : 0;
    }

    uint32_t script_value_type(void* state, int32_t index)
    {
        return IsClient() && state ? ValueType(LuaTypeAt()(state, index)) : WXL_VALUE_NIL;
    }

    double script_to_number(void* state, int32_t index)
    {
        return IsClient() && state ? LuaToNumberAt()(state, index) : 0.0;
    }

    const char* script_to_string(void* state, int32_t index, uint32_t* length)
    {
        if (!IsClient() || !state) return nullptr;
        std::size_t size = 0;
        const char* text = LuaToLStringAt()(state, index, &size);
        if (length) *length = static_cast<uint32_t>(size);
        return text;
    }

    wxl_bool script_to_bool(void* state, int32_t index)
    {
        return IsClient() && state && LuaToBooleanAt()(state, index) ? WXL_TRUE : WXL_FALSE;
    }

    void script_push_number(void* state, double value)
    {
        if (IsClient() && state) LuaPushNumberAt()(state, value);
    }

    void script_push_string(void* state, const char* text, uint32_t length)
    {
        if (IsClient() && state) LuaPushLStringAt()(state, text ? text : "", text ? length : 0u);
    }

    void script_push_bool(void* state, wxl_bool value)
    {
        if (IsClient() && state) LuaPushBooleanAt()(state, value ? 1 : 0);
    }

    void script_push_nil(void* state)
    {
        if (IsClient() && state) LuaPushNilAt()(state);
    }

    void script_pop(void* state, int32_t count)
    {
        if (IsClient() && state && count > 0) LuaSetTopAt()(state, -count - 1);
    }

    void script_error(void* state, const char* message)
    {
        if (!IsClient() || !state) return;
        const char* text = message ? message : "WarcraftXL: error";
        LuaPushLStringAt()(state, text, std::strlen(text));
        LuaErrorAt()(state);
    }

    void script_run(const char* source, const char* name)
    {
        if (IsClient() && source) ScriptExecuteAt()(source, name ? name : "WarcraftXL", nullptr);
    }

    void script_create_events(const char** names, uint32_t count)
    {
        if (IsClient() && (names || count == 0)) ScriptCreateEventsAt()(names, count);
    }

    void script_signal_event(uint32_t index, void* state, int32_t arg_count)
    {
        if (IsClient() && state) ScriptSignalEventAt()(index, state, arg_count);
    }

    wxl_bool file_exists(const char* path)
    {
        if (!IsClient() || !path) return WXL_FALSE;
        const FileExists call = reinterpret_cast<FileExists>(WXL_SITE("SFile::FileExists", 0));
        return call(path) ? WXL_TRUE : WXL_FALSE;
    }

    void* file_open(const char* path)
    {
        if (!IsClient() || !path) return nullptr;
        const FileOpen call = reinterpret_cast<FileOpen>(WXL_SITE("SFileOpenFile", 0));
        void*          file = nullptr;
        return call(path, &file) ? file : nullptr;
    }

    uint64_t file_size(void* file)
    {
        if (!IsClient() || !file) return 0;
        const FileSize call = reinterpret_cast<FileSize>(WXL_SITE("SFileGetFileSize", 0));
        uint32_t       high = 0;
        const uint32_t low  = call(file, &high);
        return (static_cast<uint64_t>(high) << 32) | low;
    }

    wxl_bool file_seek(void* file, uint64_t offset)
    {
        if (!IsClient() || !file) return WXL_FALSE;
        const FileSeek call = reinterpret_cast<FileSeek>(WXL_SITE("SFile::SetFilePointer", 0));
        int32_t        high = static_cast<int32_t>(offset >> 32);
        // Method 0 is FILE_BEGIN: the offset is absolute, which is what read_at means. The answer
        // is the cursor afterwards, 0 for a seek to the start (0x00421C75: `mov eax, edi`).
        const int32_t at = call(file, static_cast<int32_t>(offset & 0xFFFFFFFFu), &high, 0);
        return wxl::core::storm::SeekLanded(static_cast<uint32_t>(at), offset) ? WXL_TRUE : WXL_FALSE;
    }

    uint32_t file_read(void* file, void* buffer, uint32_t count)
    {
        if (!IsClient() || !file || !buffer || !count) return 0;
        const FileRead call = reinterpret_cast<FileRead>(WXL_SITE("SFileReadFile", 0));
        uint32_t       read = 0;
        call(file, buffer, count, &read, nullptr, 0);
        return read;
    }

    void file_close(void* file)
    {
        if (!IsClient() || !file) return;
        reinterpret_cast<FileClose>(WXL_SITE("SFileCloseFile", 0))(file);
    }

    wxl_bool archive_mount(const char* path, int32_t priority)
    {
        if (!IsClient() || !path) return WXL_FALSE;
        const ArchiveOpen call =
            reinterpret_cast<ArchiveOpen>(WXL_SITE("SFile::OpenArchive", 0));
        void* archive = nullptr;
        return call(path, priority, 0, &archive) ? WXL_TRUE : WXL_FALSE;
    }

    const char* storage_base_path()
    {
        if (!IsClient()) return nullptr;
        return reinterpret_cast<const char*>(WXL_STATIC("SFile::m_basePath"));
    }

    uint32_t archive_list(wxl_core_name_fn fn, void* user)
    {
        if (!IsClient() || !fn) return 0;

        // The records first, walked in order; the listing opens files, which may reach the set.
        const std::vector<void*> records = ArchiveRecords();
        using List = int32_t(__stdcall*)(void* archive, wxl_core_name_fn callback, void* param);

        // SFile::List takes an SArchive: its type (0, an MPQ) then the record, as OpenEx reads it
        // ([eax], [eax+4] at 0x00424C6F). stdcall: `ret 0xc` at 0x004250C8.
        const List list  = reinterpret_cast<List>(WXL_SITE("SFile::List", 0));
        uint32_t   listed = 0;
        for (void* record : records)
        {
            struct
            {
                uint32_t type;
                void*    archive;
            } archive{0, record};
            if (record && list(&archive, fn, user)) ++listed;
        }
        return listed;
    }

    wxl_bool gx_device_exists()
    {
        return IsClient() && GxDevice() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool gx_has_context()
    {
        // GxHasContext tests the device pointer itself, so it is safe to call without one.
        if (!IsClient()) return WXL_FALSE;
        const GxFlag call = reinterpret_cast<GxFlag>(WXL_SITE("GxHasContext", 0));
        return call() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool gx_window_visible()
    {
        if (!IsClient()) return WXL_FALSE;
        const GxFlag call = reinterpret_cast<GxFlag>(WXL_SITE("GxIsWindowVisible", 0));
        return call() ? WXL_TRUE : WXL_FALSE;
    }

    wxl_bool gx_window_size(uint32_t* width, uint32_t* height)
    {
        // GxCapsWindowSizeInScreenCoords dereferences g_theGxDevicePtr without testing it
        // (0x00493BF3 `mov ecx,[g_theGxDevicePtr]` then `mov eax,[ecx]`), so the test is here.
        if (!IsClient() || !width || !height || !GxDevice()) return WXL_FALSE;
        const GxWindowSize call =
            reinterpret_cast<GxWindowSize>(WXL_SITE("GxCapsWindowSizeInScreenCoords", 0));
        float rect[4] = {};
        call(rect);
        const float w = rect[3] - rect[1];
        const float h = rect[2] - rect[0];
        *width  = w > 0.0f ? static_cast<uint32_t>(w + 0.5f) : 0u;
        *height = h > 0.0f ? static_cast<uint32_t>(h + 0.5f) : 0u;
        return WXL_TRUE;
    }

    // wxl.gx is served only where the client hands its device and model renderer over (x64).
    // Here the rows answer absent, and the M2 seam's body finds nothing to report.
    // Not measured on x86 yet: the reaction answers absent there.
    int32_t unit_reaction(const void*, const void*) { return -1; }

    void* gx_d3d9_device() { return nullptr; }

    void gx_invalidate_state() {}

    void* gx_object_model(const void*) { return nullptr; }

    wxl_bool gx_model_draw(const void*, wxl_gx_model_draw*) { return WXL_FALSE; }

    // wxl.m2 is served only where the client hands its appearances and models over (x64). Here
    // the rows answer absent, and the Component and AnimateMT seams find nothing to hand on.
    wxl_bool component_wearer(const void*, uint32_t, wxl_m2_equipment*) { return WXL_FALSE; }

    wxl_bool m2_item_display(uint32_t, wxl_m2_item_display*) { return WXL_FALSE; }

    const char* m2_race_prefix(uint32_t) { return nullptr; }

    void* m2_create_model(void*, const char*) { return nullptr; }

    void m2_release_model(void*) {}

    wxl_bool m2_attach_model(void*, void*, uint32_t, wxl_bool) { return WXL_FALSE; }

    void m2_detach_model(void*) {}

    void* m2_model_parent(const void*) { return nullptr; }

    void* m2_model_source(const void*) { return nullptr; }

    void m2_set_geosets(void*, uint32_t, uint32_t, wxl_bool) {}

    wxl_bool m2_replace_texture(void*, uint32_t, const char*) { return WXL_FALSE; }

    wxl_bool m2_pose_from_parent(void*) { return WXL_FALSE; }

    uint32_t m2_children(const void*, void**, uint32_t) { return 0; }

    wxl_bool m2_attach_info(const void*, uint32_t*, wxl_bool*) { return WXL_FALSE; }

    wxl_bool m2_attach_copy(void*, void*, uint32_t, wxl_bool) { return WXL_FALSE; }

    void* m2_model_file(const void*) { return nullptr; }

    wxl_bool m2_attachment_point(const void*, uint32_t, wxl_vec3*) { return WXL_FALSE; }

    wxl_bool m2_has_key_bone(const void*, uint32_t) { return WXL_FALSE; }

    wxl_bool m2_pose_on_bone(void*, uint32_t, const wxl_vec3*) { return WXL_FALSE; }

    // wxl.m2file is served only where the client hands its model loader over (x64). Here the rows
    // answer absent, and the loader seams find no file to hand on.
    wxl_bool m2_file_info(const void*, wxl_m2file*) { return WXL_FALSE; }

    void m2_file_set_size(void*, uint32_t) {}

    void m2_file_animate_bones(void*) {}

    wxl_bool object_position(const void* object, wxl_vec3* out)
    {
        if (!IsClient() || !object || !out) return WXL_FALSE;
        switch (object_type_id(object))
        {
        case WXL_OBJECT_UNIT:
        case WXL_OBJECT_PLAYER: return CallPosition(WXL_SITE("CGUnit_C::GetPosition", 0), object, out);
        case WXL_OBJECT_GAMEOBJECT: return CallPosition(WXL_SITE("CGGameObject_C::GetPosition", 0), object, out);
        case WXL_OBJECT_DYNAMIC: return CallPosition(WXL_SITE("CGDynamicObject_C::GetPosition", 0), object, out);
        case WXL_OBJECT_CORPSE:
            // The fork declares no CGCorpse_C::GetPosition; the corpse's CPassenger holds it.
            *out = Read<wxl_vec3>(object, kCorpsePosition);
            return WXL_TRUE;
        default: return WXL_FALSE;
        }
    }

    wxl_bool object_facing(const void* object, float* out)
    {
        if (!IsClient() || !object || !out) return WXL_FALSE;
        switch (object_type_id(object))
        {
        case WXL_OBJECT_UNIT:
        case WXL_OBJECT_PLAYER: return CallFacing(WXL_SITE("CGUnit_C::GetFacing", 0), object, out);
        case WXL_OBJECT_GAMEOBJECT: return CallFacing(WXL_SITE("CGGameObject_C::GetFacing", 0), object, out);
        case WXL_OBJECT_DYNAMIC: return CallFacing(WXL_SITE("CGDynamicObject_C::GetFacing", 0), object, out);
        case WXL_OBJECT_CORPSE: return CallFacing(WXL_SITE("CGCorpse_C::GetFacing", 0), object, out);
        default: return WXL_FALSE;
        }
    }

    const char* object_name(const void* object)
    {
        if (!IsClient() || !object) return nullptr;
        switch (object_type_id(object))
        {
        case WXL_OBJECT_UNIT:
        case WXL_OBJECT_PLAYER: return CallName(WXL_SITE("CGUnit_C::GetObjectName", 0), object);
        case WXL_OBJECT_GAMEOBJECT: return CallName(WXL_SITE("CGGameObject_C::GetObjectName", 0), object);
        case WXL_OBJECT_ITEM:
        case WXL_OBJECT_CONTAINER: return CallName(WXL_SITE("CGItem_C::GetObjectName", 0), object);
        default: return nullptr;
        }
    }

    // --- Memory ---------------------------------------------------------------------------

    namespace
    {
        /**
         * @brief Calls `visit` with every element of a TSList, from its terminator.
         *
         * A TSList starts with the offset of the link inside each element; a link is its
         * previous link then its next element, and the terminator's next is the first element. A
         * next with its low bit set (or null) is the terminator again: the end.
         */
        template <class F>
        void WalkList(const uint8_t* list, std::size_t terminatorNext, F&& visit)
        {
            const std::ptrdiff_t linkOffset = Read<std::ptrdiff_t>(list, 0);
            const uint8_t*       node       = Read<const uint8_t*>(list, terminatorNext);
            for (uint32_t guard = 0; node && !(reinterpret_cast<uintptr_t>(node) & 1) && guard < (1u << 20); ++guard)
            {
                visit(node);
                node = Read<const uint8_t*>(node, static_cast<std::size_t>(linkOffset) + sizeof(void*));
            }
        }

        /// The bytes a MSVC 9 std::vector reserves: its first and its end of storage, at +4 and +0xC
        /// (InitializeTables reads the hash table's at [rec+0x1C0] and [rec+0x1C4], 0x0045B8F4).
        uint64_t VectorBytes(const uint8_t* vector)
        {
            const uintptr_t first = Read<uintptr_t>(vector, 4);
            const uintptr_t end   = Read<uintptr_t>(vector, 0xC);
            return first && end > first ? end - first : 0;
        }

        /// The bytes a BitPacked* holds: its bit count at +4, rounded up to bytes.
        uint64_t BitPackedBytes(const uint8_t* bits)
        {
            return bits ? (static_cast<uint64_t>(Read<uint32_t>(bits, 4)) + 7) / 8 + 8 : 0;
        }

        /**
         * @brief What one ARCHIVEREC says of itself.
         *
         * The layout is the fork's (SFileArchives.h, measured for Win32 in its comments), checked
         * against the code that reads it: the tables-initialised flag at 0x1D9, the unpacked hash
         * table (a vector of 16-byte entries) at 0x1BC, the packed block table at 0x1A4 and the
         * header's hash table size at 0x40 (all read by ARCHIVEREC::InitializeTables, 0x0045B8D2
         * to 0x0045B926). The priority is at 0x1C, the name inline at 0x84, the header's block
         * table size at 0x44, the unpacked block table (0x31-byte entries) at 0x1AC, the packed
         * hash table at 0x1A8 and the listfile, a std::map of names, at 0x204 with its size at +8.
         * A listfile name is counted at 96 bytes: a map node and the name's heap block.
         */
        bool DescribeArchive(const uint8_t* rec, wxl_core_archive* out)
        {
            if (!rec || !out) return false;
            // m_name, or a folder archive's m_directPath (a std::string at 0x188).
            out->path          = wxl::core::storm::ArchiveName(reinterpret_cast<const char*>(rec + 0x84),
                                                               wxl::core::storm::Msvc9String(rec + 0x188));
            out->priority      = Read<int32_t>(rec, 0x1C);
            out->hash_entries  = Read<uint32_t>(rec, 0x40);
            out->block_entries = Read<uint32_t>(rec, 0x44);

            uint64_t bytes = VectorBytes(rec + 0x1BC) + VectorBytes(rec + 0x1AC);
            if (const uint8_t* blocks = Read<const uint8_t*>(rec, 0x1A4))
            {
                // BlockEntryTable: its vectors of flags, file times, CRCs and MD5s at 0, 0x54,
                // 0x64 and 0x74, its packed hashes and entries at 0x48 and 0x4C; 0x88 bytes itself.
                bytes += 0x88 + VectorBytes(blocks) + VectorBytes(blocks + 0x54) + VectorBytes(blocks + 0x64) +
                         VectorBytes(blocks + 0x74) + BitPackedBytes(Read<const uint8_t*>(blocks, 0x48)) +
                         BitPackedBytes(Read<const uint8_t*>(blocks, 0x4C));
            }
            if (const uint8_t* hashes = Read<const uint8_t*>(rec, 0x1A8))
            {
                // HashEntryTable: a tag byte per slot (the count at 0x14), packed indices at 0x10.
                bytes += 0x38 + Read<uint32_t>(hashes, 0x14) + BitPackedBytes(Read<const uint8_t*>(hashes, 0x10));
            }
            bytes += static_cast<uint64_t>(Read<uint32_t>(rec, 0x204 + 8)) * 96u;
            out->bytes = bytes;
            return true;
        }

        // The client's Lua: lua_gc(L, LUA_GCCOUNT) in kilobytes and LUA_GCCOUNTB, the rest in bytes.
        using LuaGc = int32_t(__cdecl*)(void* L, int32_t what, int32_t data);
        constexpr int32_t kLuaGcCount  = 3;
        constexpr int32_t kLuaGcCountB = 4;
    }

    uint32_t gx_resources(wxl_core_gpu_fn fn, void* user)
    {
        if (!IsClient() || !fn) return 0;
        const uint8_t* device = static_cast<const uint8_t*>(GxDevice());
        if (!device) return 0;
        // EGxApi: 1 is Direct3D 9, 2 Direct3D 9Ex; the others hold no IDirect3D resources.
        const int32_t api = Read<int32_t>(device, WXL_FIELD("CGxDevice", "m_api"));
        if (api != 1 && api != 2) return 0;

        uint32_t told = 0;
        // The textures: each one created holds its IDirect3DBaseTexture9 in m_apiSpecificData; one
        // waiting to be created again holds a placeholder there and is skipped.
        WalkList(device + WXL_FIELD("CGxDevice", "m_textures"),
                 WXL_FIELD("CGxDevice", "m_textures.m_terminator.m_next") - WXL_FIELD("CGxDevice", "m_textures"),
                 [&](const uint8_t* texture) {
                     if (Read<int8_t>(texture, WXL_FIELD("CGxTex", "m_needsCreation"))) return;
                     auto* object = Read<IDirect3DBaseTexture9*>(texture, WXL_FIELD("CGxTex", "m_apiSpecificData"));
                     wxl_core_gpu_resource resource{};
                     if (object && wxl::core::gpu::DescribeTexture(object, &resource))
                     {
                         fn(&resource, user);
                         ++told;
                     }
                 });
        // The pools: each one holds its IDirect3DVertexBuffer9 or IndexBuffer9 in m_apiSpecific.
        WalkList(device + WXL_FIELD("CGxDevice", "m_poolList"),
                 WXL_FIELD("CGxDevice", "m_poolList.m_terminator.m_next") - WXL_FIELD("CGxDevice", "m_poolList"),
                 [&](const uint8_t* pool) {
                     auto* object = Read<IDirect3DResource9*>(pool, WXL_FIELD("CGxPool", "m_apiSpecific"));
                     wxl_core_gpu_resource resource{};
                     if (object && wxl::core::gpu::DescribeBuffer(object, &resource))
                     {
                         fn(&resource, user);
                         ++told;
                     }
                 });
        return told;
    }

    wxl_bool sound_heap(uint64_t* current, uint64_t* peak)
    {
        if (!IsClient() || !current || !peak) return WXL_FALSE;
        // FMOD::gGlobal, the sound engine's globals, as FMOD_Memory_allocC loads it: `mov eax,
        // [gGlobal]` (A1, then the address) at +0x11. Its gSystemPool at +4 is the MemPool every
        // FMOD allocation goes through, which counts what it holds at +0x1C and the most it held
        // at +0x120 (MemPool::alloc: `add [esi+0x1c], edx`, then `cmp eax, [esi+0x120]` and
        // `mov [esi+0x120], eax`, 0x008CFE9C to 0x008CFEB7).
        const uint8_t* load = reinterpret_cast<const uint8_t*>(WXL_SITE("FMOD_Memory_allocC", 0x11));
        if (load[0] != 0xA1) return WXL_FALSE;
        const uint8_t* global = Read<const uint8_t*>(Read<const void*>(load, 1), 0);
        const uint8_t* pool   = global ? Read<const uint8_t*>(global, 4) : nullptr;
        if (!pool) return WXL_FALSE;
        *current = Read<uint32_t>(pool, 0x1C);
        *peak    = Read<uint32_t>(pool, 0x120);
        return WXL_TRUE;
    }

    uint64_t script_heap()
    {
        void* L = IsClient() ? script_state() : nullptr;
        if (!L) return 0;
        const LuaGc gc = reinterpret_cast<LuaGc>(WXL_SITE("lua_gc", 0));
        return static_cast<uint64_t>(gc(L, kLuaGcCount, 0)) * 1024u + static_cast<uint64_t>(gc(L, kLuaGcCountB, 0));
    }

    uint32_t archive_tables(wxl_core_archive_fn fn, void* user)
    {
        if (!IsClient() || !fn) return 0;
        uint32_t told = 0;
        for (void* record : ArchiveRecords())
        {
            wxl_core_archive archive{};
            if (!DescribeArchive(static_cast<const uint8_t*>(record), &archive)) continue;
            fn(&archive, user);
            ++told;
        }
        return told;
    }

    wxl_bool archive_of(void* archive, wxl_core_archive* out)
    {
        // An SArchive: its type, 0 for an MPQ, then its ARCHIVEREC* (SFile2-Core.h).
        if (!IsClient() || !archive || !out || Read<uint32_t>(archive, 0) != 0) return WXL_FALSE;
        return DescribeArchive(Read<const uint8_t*>(archive, 4), out) ? WXL_TRUE : WXL_FALSE;
    }

    uint32_t client_dbs(wxl_core_db_fn fn, void* user)
    {
        if (!IsClient() || !fn) return 0;
        // Every store shares WowClientDB_Base's counts; a plain store has its records at 0x1C and
        // its index by id at 0x20, a compressed one its records (maybe compressed) at 0x18 and its
        // row pointers -- the index by id after them, in the same block -- at 0x1C.
        struct Row
        {
            uintptr_t   address;
            const char* file;
            uint32_t    row_size;
            bool        rle;
        };
        static const Row kRows[] = {
#define WXL_DB_RLE_plain false
#define WXL_DB_RLE_rle   true
#define WXL_CLIENT_DB(global, Rec, store, file, row_size) {WXL_STATIC(#global), file, row_size, WXL_DB_RLE_##store},
#include "wxl/core/client_dbs.def"
#undef WXL_CLIENT_DB
#undef WXL_DB_RLE_rle
#undef WXL_DB_RLE_plain
        };
        constexpr std::size_t kPlainRecords = WXL_FIELD("WowClientDB_SpellRec", "m_records");
        constexpr std::size_t kPlainIndex   = WXL_FIELD("WowClientDB_SpellRec", "m_recordsById");
        constexpr std::size_t kRleData      = WXL_FIELD("WowClientDB_RLE_SpellRec", "m_data");
        constexpr std::size_t kRleRows      = WXL_FIELD("WowClientDB_RLE_SpellRec", "m_records");

        uint32_t told = 0;
        for (const Row& row : kRows)
        {
            const void* store   = reinterpret_cast<const void*>(row.address);
            const int32_t count = Read<int32_t>(store, WXL_FIELD("WowClientDB_SpellRec", "m_numRecords"));
            const int32_t minId = Read<int32_t>(store, WXL_FIELD("WowClientDB_SpellRec", "m_minID"));
            const int32_t maxId = Read<int32_t>(store, WXL_FIELD("WowClientDB_SpellRec", "m_maxID"));
            const uint64_t ids  = count > 0 && maxId >= minId ? static_cast<uint64_t>(maxId - minId) + 1 : 0;

            wxl_core_db db{};
            db.file        = row.file;
            db.loaded      = Read<int32_t>(store, WXL_FIELD("WowClientDB_SpellRec", "m_loaded")) ? WXL_TRUE : WXL_FALSE;
            db.records     = count > 0 ? static_cast<uint32_t>(count) : 0u;
            db.record_size = row.row_size;
            db.blocks[0]   = Read<const void*>(store, row.rle ? kRleData : kPlainRecords);
            db.estimates[0] = static_cast<uint64_t>(db.records) * row.row_size;
            db.blocks[1]   = Read<const void*>(store, WXL_FIELD("WowClientDB_SpellRec", "m_strings"));
            db.blocks[2]   = Read<const void*>(store, row.rle ? kRleRows : kPlainIndex);
            db.estimates[2] = (row.rle ? db.records + ids : ids) * sizeof(void*);
            fn(&db, user);
            ++told;
        }
        return told;
    }
}

// --- Asset: models, map objects, doodads (asset.def) ----------------------------------------

namespace
{
    // The allocator of model data: 16-byte aligned blocks of Storm's heap (SMemAlloc).
    using AllocA16Fn = void*(__cdecl*)(uint32_t size, const char* file, int32_t line);
    using FreeA16Fn  = void(__cdecl*)(void* block);
}

namespace wxl::core::binding
{
    const char* model_path(const void* shared)
    {
        if (!IsClient() || !shared) return nullptr;
        return static_cast<const char*>(shared) + WXL_FIELD("CM2Shared", "m_fileName");
    }

    const void* model_bytes(const void* shared, uint32_t* size)
    {
        if (!IsClient() || !shared) return nullptr;
        if (size) *size = Read<uint32_t>(shared, WXL_FIELD("CM2Shared", "m_fileSize"));
        return Read<const void*>(shared, WXL_FIELD("CM2Shared", "m_data"));
    }

    wxl_bool model_replace_bytes(void* shared, const void* bytes, uint32_t size)
    {
        if (!IsClient() || !shared || !bytes || !size) return WXL_FALSE;
        const AllocA16Fn alloc = reinterpret_cast<AllocA16Fn>(WXL_SITE("SIMD::AllocA16", 0));
        const FreeA16Fn  release = reinterpret_cast<FreeA16Fn>(WXL_SITE("SIMD::FreeA16", 0));
        void* copy = alloc(size, __FILE__, __LINE__);
        if (!copy) return WXL_FALSE;
        std::memcpy(copy, bytes, size);
        char* base = static_cast<char*>(shared);
        void* read = Read<void*>(shared, WXL_FIELD("CM2Shared", "m_data"));
        std::memcpy(base + WXL_FIELD("CM2Shared", "m_data"), &copy, sizeof copy);
        std::memcpy(base + WXL_FIELD("CM2Shared", "m_fileSize"), &size, sizeof size);
        if (read) release(read);
        return WXL_TRUE;
    }

    const char* map_obj_path(const void* map_obj)
    {
        if (!IsClient() || !map_obj) return nullptr;
        return static_cast<const char*>(map_obj) + WXL_FIELD("CMapObj", "m_wmoName");
    }

    const void* map_obj_bytes(const void* map_obj, uint32_t* size)
    {
        if (!IsClient() || !map_obj) return nullptr;
        if (size) *size = Read<uint32_t>(map_obj, WXL_FIELD("CMapObj", "m_wmoFileSize"));
        return Read<const void*>(map_obj, WXL_FIELD("CMapObj", "m_pWmoData"));
    }

    wxl_bool doodad_position(const void* def, wxl_vec3* out)
    {
        if (!IsClient() || !def || !out) return WXL_FALSE;
        out->x = Read<float>(def, WXL_FIELD("CMapDoodadDef", "m_position.x"));
        out->y = Read<float>(def, WXL_FIELD("CMapDoodadDef", "m_position.y"));
        out->z = Read<float>(def, WXL_FIELD("CMapDoodadDef", "m_position.z"));
        return WXL_TRUE;
    }
}
