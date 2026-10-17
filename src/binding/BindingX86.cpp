// The x86 realisation of the client binding: memory reads at measured offsets, calls at resolved addresses.
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

#include "binding/Binding.hpp"
#include "binding/BindingX86.hpp"

#include "common/Log.hpp"
#include "runtime/Core.hpp"
#include "runtime/Image.hpp"

#include "wxl/events.h"
#include "wxl/offsets/layout.hpp"
#include "wxl/offsets/site.hpp"

#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

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
}

namespace wxl::core::binding::x86
{
    void Prepare() { g_mainThread = GetCurrentThreadId(); }
}

namespace wxl::core::binding
{
    // --- the field rows, generated from the table --------------------------------------------

#define WXL_BINDING(name, ret, params, args, absent)

#define WXL_BINDING_FIELD(name, ret, Type, member, absent)                                      \
    ret name(const void* object)                                                                 \
    {                                                                                            \
        return object && IsClient() ? Read<ret>(object, WXL_FIELD(#Type, member)) : (absent);    \
    }

#define WXL_BINDING_FIELD_VIA(name, ret, Type, via, ViaType, member, absent)                    \
    ret name(const void* object)                                                                 \
    {                                                                                            \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, via)) : nullptr;           \
        return block ? Read<ret>(block, WXL_FIELD(#ViaType, member)) : (absent);                 \
    }

#define WXL_BINDING_FIELD_VIA_INDEX(name, ret, Type, via, ViaType, member)                      \
    wxl_bool name(const void* object, uint32_t index, ret* out)                                  \
    {                                                                                            \
        constexpr std::size_t count = WXL_MEMBER_SIZE(#ViaType, member) / sizeof(ret);           \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, via)) : nullptr;           \
        if (!block || !out || index >= count) return WXL_FALSE;                                  \
        *out = Read<ret>(block, WXL_FIELD(#ViaType, member) + index * sizeof(ret));              \
        return WXL_TRUE;                                                                         \
    }

#define WXL_BINDING_FIELD_VIA_AT(name, ret, Type, via, ViaType, member, Elem, elem_member)      \
    wxl_bool name(const void* object, uint32_t index, ret* out)                                  \
    {                                                                                            \
        constexpr std::size_t stride = WXL_TYPE_SIZE(#Elem);                                     \
        constexpr std::size_t count  = WXL_MEMBER_SIZE(#ViaType, member) / stride;               \
        const void* block = IsClient() ? Via(object, WXL_FIELD(#Type, via)) : nullptr;           \
        if (!block || !out || index >= count) return WXL_FALSE;                                  \
        *out = Read<ret>(block, WXL_FIELD(#ViaType, member) + index * stride +                   \
                                     WXL_FIELD(#Elem, elem_member));                             \
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

    uint64_t ui_target_guid(void)
    {
        return IsClient() ? *reinterpret_cast<const uint64_t*>(WXL_STATIC("CGGameUI::m_lockedTarget")) : 0;
    }

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
}
