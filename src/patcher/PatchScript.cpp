// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// PatchScript registry.

#include "patcher/PatchScript.hpp"

#include <vector>

namespace
{
    /**
     * @brief Returns the script list, held in a function-local static safe under any constructor init order.
     * @return Reference to the registered-script vector.
     */
    std::vector<wxl::patcher::PatchScript*>& List()
    {
        static std::vector<wxl::patcher::PatchScript*> v;
        return v;
    }
}

namespace wxl::patcher
{
    /** @brief Registers this script with the global registry. */
    PatchScript::PatchScript() { registry::Add(this); }

    namespace registry
    {
        /**
         * @brief Adds a script to the registry.
         * @param script  script to register.
         */
        void Add(PatchScript* script) { List().push_back(script); }

        /**
         * @brief Returns all registered scripts.
         * @return Span over the registered scripts.
         */
        std::span<PatchScript* const> Scripts() { return List(); }
    }
}
