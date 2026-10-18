// The link stamp of this DLL, read out of its own PE headers: which binary is actually running.
// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

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
