// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The crash report: what the log says about an exception nothing in the process handled, before
// the client's own handler writes Errors\ and ends the process.

#pragma once

#include <cstdint>

namespace wxl::core::crash
{
    /// The tag of the report's lines.
    constexpr const char* kTag = "crash";

    /// The tag of the report of a fatal error the client displays itself.
    constexpr const char* kFatalTag = "fatal";

    /// The tag of the line of an error the client displays and goes on from.
    constexpr const char* kClientTag = "client";

    /**
     * @brief Puts the runtime's filter at the top of the process's unhandled-exception chain,
     *        ahead of whatever filter was there, which it calls after writing its report.
     *
     * The client sets its own filter (Storm's, which writes Errors\) once it starts, over this
     * one; so the runtime takes the top again at every stage, chaining to the client's. Safe
     * under the loader lock, and called again at will: a second call only moves the filter the
     * client set since behind this one.
     */
    void Install();

    /**
     * @brief Tells the log about an error the client is about to display (Storm's
     *        SErrDisplayError), before its dialog and Errors\ file.
     *
     * A fatal error is no exception: the client ends the process itself afterwards, and the
     * crash filter never runs. So this writes the report a crash would get, flushed: the
     * message, the code, where the client said it came from, the stack, the mod whose code was
     * running, the last events raised to mods, and the session's end line. A recoverable error
     * is one warning line. Nothing when the crash report already ran: Storm's exception filter,
     * which runs after it, displays the same fault again.
     *
     * @param code         the Storm error code, 0x85100086 for SErrDisplayAppFatal.
     * @param file         the source file, or Storm's text for its own kinds (@p line < 0).
     * @param line         the source line, or a negative Storm kind.
     * @param message      the formatted message the dialog shows.
     * @param recoverable  whether the client goes on afterwards.
     */
    void ClientError(uint32_t code, const char* file, int32_t line, const char* message, bool recoverable);
}
