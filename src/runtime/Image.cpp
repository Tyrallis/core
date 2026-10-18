// The process image: whether it is the client build the offset tables describe, and whether the
// patcher gave it the entry trampoline.
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

#include "runtime/Image.hpp"

#include <windows.h>
#include <cstring>

namespace
{
    /// IMAGE_FILE_HEADER::TimeDateStamp of Wow.exe 3.3.5a build 12340.
    constexpr DWORD kClientTimeStamp = 0x4C2452FE;

    /// The section the patcher appends for the entry trampoline.
    constexpr char kEntrySection[] = ".wxle";

    /// The executable's NT headers, or null when the image in memory is not a 32-bit PE.
    const IMAGE_NT_HEADERS32* NtHeaders()
    {
        const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        if (!base) return nullptr;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
        return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
    }
}

namespace wxl::core::image
{
    namespace detail
    {
        std::atomic<signed char> g_isClient{-1};

        bool ReadIsClient()
        {
            const IMAGE_NT_HEADERS32* nt = NtHeaders();
            return nt && nt->FileHeader.TimeDateStamp == kClientTimeStamp;
        }
    }

    uint32_t Build() { return IsClient() ? kBuild : 0; }

    bool HasEntryTrampoline()
    {
        const IMAGE_NT_HEADERS32* nt = NtHeaders();
        if (!nt) return false;

        const DWORD entry = nt->OptionalHeader.AddressOfEntryPoint;
        const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        {
            if (memcmp(sec[i].Name, kEntrySection, sizeof kEntrySection) != 0) continue;
            return entry >= sec[i].VirtualAddress
                && entry < sec[i].VirtualAddress + sec[i].Misc.VirtualSize;
        }
        return false;
    }
}
