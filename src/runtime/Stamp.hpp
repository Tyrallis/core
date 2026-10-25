// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The link stamp of this DLL, read out of its own PE headers: which binary is actually running.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::core::stamp
{
    /// Longest string Describe writes, terminator included.
    constexpr std::size_t kDescriptionSize = 40;

    /**
     * @brief IMAGE_FILE_HEADER::TimeDateStamp of the module this code is part of, which the
     *        linker set when it wrote the file, or 0 when the headers cannot be read.
     *
     * The module is this DLL, not the process executable: the symbol the linker resolves to the
     * image base is the one of the binary the code was linked into.
     */
    uint32_t Value();

    /**
     * @brief Writes the stamp as "0x6AD435F0 (2026-10-18 03:17:04 UTC)" into @p out.
     *
     * Formats the date itself rather than through the CRT's time functions, so it is safe to
     * call under the loader lock. Always NUL-terminates when @p size is not 0.
     * @param out   destination buffer; kDescriptionSize bytes hold any stamp.
     * @param size  size of @p out in bytes.
     */
    void Describe(char* out, std::size_t size);
}
