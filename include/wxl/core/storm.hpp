// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// How the client's file layer (Storm's SFile) answers, read the same way by both realisations of
// the storage binding rows.

#pragma once

#include <cstdint>
#include <cstring>

namespace wxl::core::storm
{
    /**
     * @brief Whether SFile::SetFilePointer put the cursor where it was asked to.
     *
     * The call answers where the cursor is afterwards, the low 32 bits of it, and has no failure
     * value: a seek to 0 answers 0. An archive's file keeps a 32-bit cursor (Mopaq's
     * SFileSetFilePointer stores the distance and answers it), so an offset past 4 GB cannot
     * land.
     */
    inline bool SeekLanded(uint32_t answered, uint64_t asked)
    {
        return asked <= 0xFFFFFFFFu && answered == static_cast<uint32_t>(asked);
    }

    /**
     * @brief The name an archive record is known by.
     *
     * Its m_name, the path it was opened by. A folder opened as an archive (a "Patch-4.MPQ" that
     * is a directory: OpenDirectPathAsArchive) leaves m_name empty and keeps the folder in
     * m_directPath, with a trailing backslash, which tells it from an archive file.
     */
    inline const char* ArchiveName(const char* name, const char* direct_path)
    {
        if (name && *name) return name;
        return direct_path ? direct_path : "";
    }

    /**
     * @brief The text of a std::string of the stock client's CRT (MSVC 9, 32-bit), 0x1C bytes.
     *
     * Its buffer at +4 holds the text while the capacity at +0x18 is under 16, and points to it
     * otherwise; the length is at +0x14 (FindFile reads a name so: +0x20 and +0x34 of an entry
     * whose string starts at +0x1C).
     */
    inline const char* Msvc9String(const uint8_t* string)
    {
        if (!string) return "";
        uint32_t capacity = 0;
        std::memcpy(&capacity, string + 0x18, sizeof capacity);
        if (capacity < 16) return reinterpret_cast<const char*>(string + 4);
        uint32_t text = 0;
        std::memcpy(&text, string + 4, sizeof text);
        return text ? reinterpret_cast<const char*>(static_cast<uintptr_t>(text)) : "";
    }
}
