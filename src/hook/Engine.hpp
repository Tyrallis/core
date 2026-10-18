// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 WarcraftXL
//
// The hook engine: detour chains over MinHook, attached and detached one link at a time.

#pragma once

#include <cstddef>
#include <cstdint>

/// Detour chains over MinHook.
///
/// Several parties may detour one address. MinHook holds a single detour per target, so the
/// parties are chained: each one's `original` points at the next party's detour and the last
/// one's at MinHook's trampoline, which runs the engine function. Not calling `original`
/// suppresses the parties behind and the engine function.
///
/// MinHook is never pointed at a party. Its detour is a six-byte stub per chain, `jmp [slot]`,
/// and the slot holds the current head. Attaching or detaching a link, the head included, is
/// then a pointer store: nothing is re-patched while calls may be running through the chain.
/// The stores are aligned pointer writes, so a call in flight reads either the old successor or
/// the new one, never a torn pointer.
namespace wxl::hook
{
    /// A live link in a chain. 0 means the attach did not take.
    using Id = uint32_t;

    constexpr Id  kNone            = 0;
    /// Lower runs first, closest to the caller. Equal values keep attach order.
    constexpr int kDefaultPriority = 0;

    /**
     * @brief Initialises MinHook. Idempotent.
     * @return true when the engine is usable.
     */
    bool Init();

    /**
     * @brief Attaches one detour to an address and enables it at once.
     *
     * The first attach on an address patches it; later ones only relink the chain.
     * @param name      label for the log.
     * @param target    engine function address.
     * @param detour    replacement function.
     * @param original  receives the next link in the chain, ending at the engine function; the
     *                  chain owns the variable and rewrites it whenever its shape changes.
     * @param priority  chain position; see kDefaultPriority.
     * @return the link, or kNone when the address cannot be patched or an argument is null.
     */
    Id Attach(const char* name, void* target, void* detour, void** original,
              int priority = kDefaultPriority);

    /**
     * @brief Typed attach: the detour and the original share one function type.
     */
    template <class Fn>
    Id Attach(const char* name, uintptr_t target, Fn* detour, Fn** original,
              int priority = kDefaultPriority)
    {
        return Attach(name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
                      reinterpret_cast<void**>(original), priority);
    }

    /**
     * @brief Removes one link. The chain closes over it; the links behind keep working.
     *
     * The detached party's `original` is left as it was, so a call still running through its
     * detour completes. An address whose last link goes stays patched and passes straight
     * through to the engine function.
     * @return false when the id is not live.
     */
    bool Detach(Id id);

    /// Live links on an address.
    size_t Count(void* target);
}
