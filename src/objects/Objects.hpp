// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl.object on this runtime: the service instance, and the client binding behind it.

#pragma once

#include "binding/Binding.hpp"

#include "wxl/runtime/object_service.hpp"

#include <cstdint>

namespace wxl::core::objects
{
    /**
     * @brief The leaf reads of wxl.object, over wxl::core::binding.
     *
     * A FIELD row of object_reads.def is the binding fact of the same name, defined here; every
     * other read is written in Reads.cpp. The same code on both builds: what differs is how the
     * binding under it reads the client.
     */
    class ClientObjects final : public wxl::runtime::ObjectBinding
    {
    public:
        wxl_guid        Guid(void* object) const override;
        wxl_object_type Type(void* object) const override;
        bool            Name(void* object, const char*& out, uint32_t& size) const override;
        bool            UnitTarget(void* object, wxl_guid& out) const override;
        void*           LocalPlayer() const override;
        void*           FindByGuid(wxl_guid guid) const override;
        uint32_t*       Descriptor(void* object) const override;
        bool WatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local) const override;
        bool UnwatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local) const override;
        void Report(const char* line) const override;

        // A field row is the binding fact of its name, read as is; a client or cache row is
        // written in Reads.cpp; a shared row is the shared runtime's own.
#define WXL_OBJECT_READ(kinds, member, Method, type, source, Cxx, cxx, doc)       \
    WXL_OBJECT_FIELDS(source, bool Method(void* object, type& out) const override \
                              {                                                   \
                                  out = binding::member(object);                  \
                                  return true;                                    \
                              })                                                  \
    WXL_OBJECT_WRITTEN(source, bool Method(void* object, type& out) const override;)
#define WXL_OBJECT_READ_AT(kinds, member, Method, type, source, Cxx, cxx, Arg, arg, doc) \
    WXL_OBJECT_WRITTEN(source, bool Method(void* object, Arg value, type& out) const override;)
#include "wxl/runtime/object_reads.def"
#undef WXL_OBJECT_READ
#undef WXL_OBJECT_READ_AT
    };

    /// The one object service of this process, built on first use over ClientObjects.
    wxl::runtime::ObjectService& Service();

    /// The table published under WXL_OBJECT_SERVICE.
    const wxl_object_api* Api();

    // What the seams drive: the ObjectMgr seams track an object when the client builds it and
    // untrack it when the client lets it go, so a handle kept past that point reads as absent.
    wxl_handle Track(void* object);
    void       Untrack(void* object);
    void       Untrack(wxl_handle handle);
    bool       IsTracked(void* object);
    wxl_handle HandleOf(void* object);
    wxl_handle FromGuid(uint64_t guid);

    /**
     * @brief Has the client report the changes of one element of a kind's fields, from now on.
     *
     * Set on the kind at once while the client is in a world, and again whenever it enters
     * one; a local element on the active player, whenever the client builds it or brings it
     * back. The reports become ObjectFieldChanged (seams/ObjectMgr.cpp, which keeps the list),
     * raised to the mods whose watch covers them.
     */
    bool WatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local);

    /// Takes the element's handler back from the client and stops setting it again: no mod
    /// watches it any more.
    bool UnwatchElement(wxl_object_type kind, uint32_t first, uint32_t dwords, bool local);
}
