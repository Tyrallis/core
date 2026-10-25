// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.footprint service: Storm's allocations by source file, the client's big consumers and
// the address space, the same on both builds.

#pragma once

#include "wxl/events.h"
#include "wxl/services/footprint.h"

#include <cstddef>
#include <cstdint>

namespace wxl::core::services
{
    /// The table, WXL_FOOTPRINT_SERVICE version WXL_FOOTPRINT_SERVICE_VERSION.
    const wxl_footprint_api& FootprintApi();

    namespace footprint
    {
        // --- what the Storm seams tell it, on any thread --------------------------------------
        //
        // Each returns at once while accounting is off: one relaxed load.

        /// SMemAlloc answered @p block for @p bytes, asked by @p file.
        void Allocated(void* block, size_t bytes, const char* file);

        /// SMemFree is about to free @p block.
        void Freeing(void* block);

        /// SMemReAlloc was entered with @p block: what it holds leaves the counts until
        /// Reallocated says what the call left.
        void Reallocating(void* block, uint32_t flags);

        /// SMemReAlloc answered @p result for @p block, @p bytes asked by @p file.
        void Reallocated(void* result, void* block, size_t bytes, const char* file);

        // --- what the runtime asks of it ---------------------------------------------------

        /// Whether accounting runs.
        bool Running();

        /// Storm's live bytes and blocks, and what the accounting's own tables hold: three
        /// relaxed loads, for a graph. False, and nothing written, while accounting is off.
        bool Totals(uint64_t* bytes, uint64_t* count, uint64_t* tables);

        /// The bytes Storm handed out for @p block while accounting ran, or false when unknown.
        bool BlockSize(const void* block, uint64_t* size);

        /**
         * @brief Whether MemoryPressure is due: a threshold is armed, about a second has passed
         *        since the last look, and the largest free block just fell under it.
         *
         * Main thread, from a seam that runs every frame. @p out is filled when it answers true.
         */
        bool PressureDue(wxl_memory_pressure_args* out);

        /// A mod is disabled or unloaded: its reports and its threshold go.
        void ForgetMod(uint32_t mod);

        /// Back to nothing counted, reported or armed. The harness, between checks.
        void Reset();
    }
}
