// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// PatchScript: base class a patcher script subclasses to declare static PE edits.

#pragma once

#include <span>

#include "patcher/PeImage.hpp"

/**
 * @brief Base class for a patcher script: a subclass self-registers via its file-scope constructor and
 *        the patcher runs every registered script against the PE.
 */
namespace wxl::patcher
{
    class PatchScript
    {
    public:
        virtual ~PatchScript() = default;

        /**
         * @brief Returns a short identifier for the patcher log.
         * @return Script name.
         */
        virtual const char* name() const = 0;

        /**
         * @brief Applies this script's edits to the PE.
         * @param pe  PE image to edit.
         * @return True on success; false aborts the whole patch run.
         */
        virtual bool Apply(PeImage& pe) const = 0;

    protected:
        /** @brief Registers this script with the global registry. */
        PatchScript();
    };

    namespace registry
    {
        /**
         * @brief Adds a script to the registry.
         * @param script  script to register.
         */
        void Add(PatchScript* script);

        /**
         * @brief Returns all registered scripts.
         * @return Span over the registered scripts.
         */
        std::span<PatchScript* const> Scripts();
    }
}
