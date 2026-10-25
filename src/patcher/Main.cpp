// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// wxl-core-patcher: add the WarcraftXL.dll imports and the entry-point trampoline to the client PE,
// and run every registered PatchScript.

#include "patcher/PatchScript.hpp"
#include "patcher/PeImage.hpp"

#include "common/Log.hpp"

#include <windows.h>
#include <io.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    constexpr char kTag[]          = "patcher";
    constexpr char kDllName[]      = "WarcraftXL.dll";
    constexpr char kAnchorName[]   = "WarcraftXL";      // the IAT anchor: imported so the DLL is mapped
    constexpr char kEntryName[]    = "WarcraftXLEntry"; // called by the trampoline before the client's OEP
    constexpr char kImportSection[] = ".wxl";  // the imports of a fresh patch (both symbols)
    constexpr char kUpgradeSection[] = ".wxli"; // the entry import added over an older patch
    constexpr char kEntrySection[]  = ".wxle"; // the entry-point trampoline

    /**
     * @brief Reads an entire file into a byte buffer.
     * @param path  file path to read.
     * @return File contents, or an empty buffer on failure.
     */
    std::vector<uint8_t> ReadAll(const char* path)
    {
        std::vector<uint8_t> data;
        FILE* f = nullptr;
        if (fopen_s(&f, path, "rb") != 0 || !f) return data;
        long size = -1;
        if (fseek(f, 0, SEEK_END) == 0) size = ftell(f);
        if (size < 0 || fseek(f, 0, SEEK_SET) != 0) { fclose(f); return data; }
        data.resize(static_cast<size_t>(size));
        if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
        fclose(f);
        return data;
    }

    /**
     * @brief Writes a byte buffer to a file atomically: the bytes go to a sibling temp file,
     *        flushed to disk, then swapped into place. The target is either fully replaced or
     *        untouched - a crash or short write can never leave it truncated.
     * @param path  file path to write.
     * @param data  bytes to write.
     * @return True on success, false on any I/O failure (the temp file is removed).
     */
    bool WriteAll(const char* path, const std::vector<uint8_t>& data)
    {
        const std::string tmp = std::string(path) + ".wxltmp";
        FILE* f = nullptr;
        if (fopen_s(&f, tmp.c_str(), "wb") != 0 || !f) return false;
        const bool written = fwrite(data.data(), 1, data.size(), f) == data.size()
                          && fflush(f) == 0
                          && _commit(_fileno(f)) == 0;
        const bool closed = fclose(f) == 0;
        if (!written || !closed) { remove(tmp.c_str()); return false; }
        if (!MoveFileExA(tmp.c_str(), path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        { remove(tmp.c_str()); return false; }
        return true;
    }

    /**
     * @brief Appends the entry-point trampoline and repoints the entry point at it.
     *
     * The cave calls the DLL's entry export through its IAT slot, then jumps to the original
     * entry point. It holds no absolute address: the slot is reached relative to the cave's own
     * location and the jump is relative, so the image may be loaded at any base.
     *
     *     call next            E8 00000000
     *   next:
     *     pop  eax             58                   eax = address of `next` at run time
     *     mov  ecx, [eax+d]    8B 88 <d>            d = iat slot - next: the loader-filled pointer
     *     call ecx             FF D1
     *     jmp  oep             E9 <rel32>
     *
     * eax and ecx carry nothing at a process entry point; the stack is left as it was found.
     * @param pe      image to edit.
     * @param iatVa   virtual address of the entry export's IAT slot.
     * @return True on success.
     */
    bool AddEntryTrampoline(wxl::patcher::PeImage& pe, uint32_t iatVa)
    {
        const uint32_t oep = pe.EntryPointVa();

        uint8_t cave[19] = {
            0xE8, 0x00, 0x00, 0x00, 0x00, // call next
            0x58,                         // pop eax
            0x8B, 0x88, 0, 0, 0, 0,       // mov ecx, [eax + disp32]
            0xFF, 0xD1,                   // call ecx
            0xE9, 0, 0, 0, 0,             // jmp rel32
        };
        const uint32_t caveVa = pe.AppendCode(cave, sizeof cave, kEntrySection);
        if (caveVa == 0) return false;

        const uint32_t nextVa = caveVa + 5;
        const uint32_t disp   = iatVa - nextVa;
        const uint32_t rel    = oep - (caveVa + sizeof cave);
        memcpy(cave + 8, &disp, 4);
        memcpy(cave + 15, &rel, 4);
        if (!pe.WriteVa(caveVa, cave, sizeof cave)) return false;

        pe.SetEntryPointVa(caveVa);
        WLOG_INFO(kTag, "entry trampoline at 0x%08X -> %s!%s via IAT 0x%08X, then OEP 0x%08X",
                  caveVa, kDllName, kEntryName, iatVa, oep);
        return true;
    }

    void Usage()
    {
        fputs("usage: wxl-core-patcher [<client exe>] [<output exe>]\n"
              "  <client exe>  the image to patch; default Wow.exe\n"
              "  <output exe>  write the patched image here and leave the input untouched;\n"
              "                without it the input is replaced and backed up as <input>.orig\n",
              stderr);
    }
}

/**
 * @brief Patches the target PE: sets large-address-aware, runs every registered PatchScript, adds the
 *        WarcraftXL.dll imports and the entry-point trampoline. In place with a backup of the original,
 *        or to a separate output file. An image patched by an older patcher (import only) gets the
 *        trampoline added; a fully patched image is left as it is.
 * @param argc  argument count.
 * @param argv  argument values; argv[1] is the target path, defaulting to "Wow.exe"; argv[2] the
 *              optional output path.
 * @return 0 on success, 1 on failure.
 */
int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    wxl::log::EnableConsole("[wxl-core-patcher] "); // progress to stdout, failures to stderr
    if (argc > 3 || (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)))
    { Usage(); return 1; }

    const char* target = argc > 1 ? argv[1] : "Wow.exe";
    const char* output = argc > 2 ? argv[2] : target;
    const bool  inPlace = output == target;
    WLOG_INFO(kTag, "patcher start, target='%s'%s%s", target, inPlace ? "" : ", output=", inPlace ? "" : output);

    std::vector<uint8_t> file = ReadAll(target);
    if (file.empty()) { WLOG_ERROR(kTag, "cannot read '%s'", target); return 1; }

    wxl::patcher::PeImage pe(file);
    if (!pe.valid()) { WLOG_ERROR(kTag, "'%s' is not a 32-bit PE", target); return 1; }

    // Always ensure 4 GB address space, even when an older patcher already injected the section.
    pe.SetLargeAddressAware();
    const bool hasImport = pe.HasSection(kImportSection);
    const bool hasEntry  = pe.HasSection(kEntrySection);
    if (hasImport && hasEntry)
    {
        if (!WriteAll(output, file)) { WLOG_ERROR(kTag, "cannot write '%s'", output); return 1; }
        WLOG_INFO(kTag, "already patched ('%s' and '%s' present), ensured large-address-aware",
                  kImportSection, kEntrySection);
        return 0;
    }

    const char* summary = nullptr;
    if (hasImport)
    {
        // An older patcher added the anchor import only: add the entry import and the trampoline.
        // Its byte scripts, if any, were applied then and are not run again.
        const char* const funcs[] = { kEntryName };
        uint32_t iat = 0;
        if (!pe.AddImport(kDllName, funcs, 1, kUpgradeSection, &iat))
        { WLOG_ERROR(kTag, "entry import injection failed"); return 1; }
        if (!AddEntryTrampoline(pe, iat))
        { WLOG_ERROR(kTag, "entry trampoline injection failed"); return 1; }
        summary = "upgraded: +entry trampoline";
    }
    else
    {
        // Apply every registered PatchScript (byte edits), then the imports and the trampoline.
        for (wxl::patcher::PatchScript* const s : wxl::patcher::registry::Scripts())
        {
            if (!s->Apply(pe))
            { WLOG_ERROR(kTag, "patch-script '%s' FAILED", s->name()); return 1; }
            WLOG_INFO(kTag, "applied patch-script '%s'", s->name());
        }

        const char* const funcs[] = { kAnchorName, kEntryName };
        uint32_t iat[2] = { 0, 0 };
        if (!pe.AddImport(kDllName, funcs, 2, kImportSection, iat))
        { WLOG_ERROR(kTag, "import injection failed"); return 1; }
        if (!AddEntryTrampoline(pe, iat[1]))
        { WLOG_ERROR(kTag, "entry trampoline injection failed"); return 1; }
        summary = "+imports, +entry trampoline";
    }

    // In place, the backup is the only recovery path if the patched image misbehaves: refuse to
    // touch the target until it is confirmed on disk.
    std::string backup;
    if (inPlace)
    {
        backup = std::string(target) + ".orig";
        if (GetFileAttributesA(backup.c_str()) == INVALID_FILE_ATTRIBUTES
            && !CopyFileA(target, backup.c_str(), TRUE))
        {
            WLOG_ERROR(kTag, "cannot back up '%s' to '%s' (win32=%lu), aborting before write",
                       target, backup.c_str(), GetLastError());
            return 1;
        }
    }

    if (!WriteAll(output, file)) { WLOG_ERROR(kTag, "cannot write '%s'", output); return 1; }

    if (inPlace)
        WLOG_INFO(kTag, "patched '%s' (%s %s!%s/%s, %zu patch-scripts, backup '%s')",
                  target, summary, kDllName, kAnchorName, kEntryName,
                  wxl::patcher::registry::Scripts().size(), backup.c_str());
    else
        WLOG_INFO(kTag, "wrote '%s' from '%s' (%s %s!%s/%s, %zu patch-scripts)",
                  output, target, summary, kDllName, kAnchorName, kEntryName,
                  wxl::patcher::registry::Scripts().size());
    return 0;
}
