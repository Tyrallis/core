// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The crash report: what the log says about an exception nothing in the process handled, before
// the client's own handler writes Errors\ and ends the process.

#pragma once

namespace wxl::core::crash
{
    /// The tag of the report's lines.
    constexpr const char* kTag = "crash";

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
}
