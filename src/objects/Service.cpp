// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// wxl.object on this runtime: the one service instance and what the seams drive on it.

#include "objects/Objects.hpp"

#include <atomic>

namespace
{
    /// Found once. Every object entering or leaving the client comes through here, and a
    /// function-local static is a thread-safe-initialisation check on each of those calls.
    /// Two threads racing to be first store the same address, so the store is ordered against
    /// nothing.
    std::atomic<wxl::runtime::ObjectService*> g_service{ nullptr };
}

namespace wxl::core::objects
{
    wxl::runtime::ObjectService& Service()
    {
        wxl::runtime::ObjectService* known = g_service.load(std::memory_order_relaxed);
        if (!known)
        {
            static ClientObjects               binding;
            static wxl::runtime::ObjectService service(binding);
            known = &service;
            g_service.store(known, std::memory_order_relaxed);
        }
        return *known;
    }

    const wxl_object_api* Api() { return Service().Api(); }

    wxl_handle Track(void* object) { return Service().Track(object); }

    void Untrack(void* object) { Service().Untrack(object); }

    void Untrack(wxl_handle handle) { Service().Untrack(handle); }

    bool IsTracked(void* object) { return Service().HandleOf(object) != WXL_HANDLE_NONE; }

    wxl_handle HandleOf(void* object) { return Service().HandleOf(object); }

    wxl_handle FromGuid(uint64_t guid) { return Service().FromGuid(guid); }
}
