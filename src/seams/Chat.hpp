// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// What the Chat seams expose to another domain's seam, on both builds.

#pragma once

#include "wxl/events.h"

#include <cstdint>

namespace wxl::core::seams::chat
{
    /**
     * @brief Tells whether an event the client fires to its interface delivers an addon message
     * of the server.
     *
     * Asked by FrameScript.vSignalEvent before anything else, with what it was handed. True when
     * the event is CHAT_MSG_ADDON and the line AddChatMessage is adding is an addon message of
     * the server: `out` is then that message as the interface receives it, its strings valid for
     * the event's call. Once per line. Anything else costs a test and answers false.
     */
    bool AddonDelivered(uint32_t index, const char* format, void* args, wxl_chat_addon_args& out);
}
