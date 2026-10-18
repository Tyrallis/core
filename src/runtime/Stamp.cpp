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

#include "runtime/Stamp.hpp"

#include <windows.h>
#include <cstdio>

/// The linker's own symbol for the base of the module holding this code: this DLL, whatever
/// executable loaded it. Image.cpp's GetModuleHandleW(nullptr) is the executable instead.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace
{
    /// Days since 1970-01-01 as a Gregorian date, for 1970 through 2106.
    void CivilFromDays(uint32_t days, uint32_t& year, uint32_t& month, uint32_t& day)
    {
        const uint32_t z   = days + 719468; // 0000-03-01 as the epoch: leap days fall at the end
        const uint32_t era = z / 146097;    // one era is 400 years, always 146097 days
        const uint32_t doe = z - era * 146097;
        const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const uint32_t mp  = (5 * doy + 2) / 153; // months counted from March

        day   = doy - (153 * mp + 2) / 5 + 1;
        month = mp < 10 ? mp + 3 : mp - 9;
        year  = yoe + era * 400 + (month <= 2 ? 1u : 0u);
    }
}

namespace wxl::core::stamp
{
    uint32_t Value()
    {
        const IMAGE_DOS_HEADER& dos = __ImageBase;
        if (dos.e_magic != IMAGE_DOS_SIGNATURE) return 0;

        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            reinterpret_cast<const uint8_t*>(&dos) + dos.e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        return nt->FileHeader.TimeDateStamp;
    }

    void Describe(char* out, std::size_t size)
    {
        if (!out || size == 0) return;

        const uint32_t value = Value();
        if (value == 0)
        {
            std::snprintf(out, size, "%s", "no PE timestamp");
            return;
        }

        uint32_t year = 0, month = 0, day = 0;
        CivilFromDays(value / 86400u, year, month, day);
        const uint32_t second = value % 86400u;
        std::snprintf(out, size, "0x%08X (%04u-%02u-%02u %02u:%02u:%02u UTC)", value, year, month,
                      day, second / 3600u, second / 60u % 60u, second % 60u);
    }
}
