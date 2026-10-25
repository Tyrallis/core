// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The client's stacked file system, as the runtime reaches it: Blizzard::File's dispatch.
//
// Every call of Blizzard::File -- open, read, seek, size, close, a folder's listing -- builds a
// FileParms and calls one verb of the filesystem at the top of the stack,
// System_File::Stacked::s_manager, which passes what it does not handle to the one below. The
// client builds the stack at its first file call (file_init swaps the initial stack for a copy of
// the base one) and frees every node with its own free() at shutdown.
//
// These are the client's declarations, member for member (Engine/Source/BlizzardCore/Source/
// System/File/Types.h and Filestack.h), so one header serves both builds: compiled 32-bit, the
// layouts are the stock client's, which the asserts below hold to the offsets the fork's headers
// carry; compiled 64-bit, they are the recompiled client's, which its WarcraftXL/Binding.cpp
// asserts against its own types.

#pragma once

#include <cstddef>
#include <cstdint>

namespace wxl::core::filestack
{
    struct Filesystem;
    struct FileParms;
    struct StreamRecord;

    /// Blizzard::File::FileInfo.
    struct FileInfo
    {
        char*    path;
        uint32_t unused_0004;
        int64_t  size;
        int32_t  attributes;
        uint8_t  unused_0014[4];
        int64_t  create_time;
        int64_t  mod_time;
        int64_t  access_time;
        int32_t  existence; ///< 0 none, 1 a file, 2 a folder
        bool     regular_file;
        uint8_t  unused_0035[3];
    };

    /// A verb: answers whether it did what the parms ask. cdecl on x86.
    using Verb = bool (*)(Filesystem* fs, FileParms* parms);

    /// Blizzard::File::Functions: one verb per operation, in this order.
    struct Functions
    {
        Verb cd, close, create, cwd, dirwalk, exists, flush, getfileinfo, getfreespace, getpos, getrootchars,
            isabspath, isreadonly, makeabspath, mkdir, move, copy, open, read, readp, rmdir, setcachemode,
            seteof, setfileinfo, setpos, unlink, write, writep, shutdown;
    };

    /// Blizzard::File::Filesystem: a node of the stack.
    struct Filesystem
    {
        Filesystem* original;
        Filesystem* next;
        Functions   verbs;
    };

    /// Blizzard::File::StreamRecord: an open file, as every verb is handed it.
    struct StreamRecord
    {
        void*    file_handle;
        int32_t  mode;
        bool     info_valid;
        uint8_t  unused_0009[3];
        uint32_t unused_000C;
        FileInfo info;
        void*    streaming;
        char*    pathname;
    };

    /// Blizzard::File::ProcessDirParms: one entry of a folder's listing, as its callback gets it.
    struct ProcessDirParms
    {
        const char* root;
        const char* filename;
        void*       cookie;
        bool        is_dir;
        uint8_t     unused_000D[3];
    };

    /// Blizzard::File::ProcessDirCallback: false stops the listing. cdecl on x86.
    using DirCallback = bool (*)(const ProcessDirParms& parms);

    /// System_File::Stacked::FileParms: what a verb is asked.
    struct FileParms
    {
        uintptr_t     verb; ///< the verb's offset in Filesystem, which PassThrough dispatches by
        char*         path;
        char*         newpath;
        StreamRecord* stream;
        FileInfo*     info;
        uint32_t      extra;
        FileInfo      info2;
        uint32_t      has;
        uint32_t      want;
        int32_t       mode;
        void*         buffer;
        int32_t       amount;
        uint8_t       unused_0064[4];
        int64_t       pos;
        int32_t       pos_mode; ///< 0 from the start, 1 from the cursor, 2 from the end
        char*         cwdbuf;
        int32_t       cwdbuflen;
        bool          recursive;
        bool          make_canonical;
        uint8_t       unused_007E[2];
        void*         cookie;
        DirCallback   dircallback;
        bool          sorted;
        bool          overwrite;
        bool          grant_everyone;
        bool          zero_fill_all;
        bool          supports_sparse_files;
        uint8_t       unused_008D[3];
    };

#if defined(_M_IX86) || defined(__i386__)
    // The stock client's layouts, as the fork's headers write them.
    static_assert(sizeof(FileInfo) == 0x38 && offsetof(FileInfo, size) == 0x08 &&
                  offsetof(FileInfo, existence) == 0x30 && offsetof(FileInfo, regular_file) == 0x34);
    static_assert(sizeof(Functions) == 29 * 4 && offsetof(Functions, open) == 0x44 &&
                  offsetof(Functions, shutdown) == 0x70);
    static_assert(sizeof(Filesystem) == 0x7C && offsetof(Filesystem, verbs) == 0x08);
    static_assert(sizeof(StreamRecord) == 0x50 && offsetof(StreamRecord, info) == 0x10 &&
                  offsetof(StreamRecord, streaming) == 0x48 && offsetof(StreamRecord, pathname) == 0x4C);
    static_assert(sizeof(ProcessDirParms) == 0x10 && offsetof(ProcessDirParms, is_dir) == 0x0C);
    static_assert(offsetof(FileParms, stream) == 0x0C && offsetof(FileParms, info) == 0x10 &&
                  offsetof(FileParms, info2) == 0x18 && offsetof(FileParms, has) == 0x50 &&
                  offsetof(FileParms, mode) == 0x58 && offsetof(FileParms, buffer) == 0x5C &&
                  offsetof(FileParms, amount) == 0x60 && offsetof(FileParms, pos) == 0x68 &&
                  offsetof(FileParms, pos_mode) == 0x70 && offsetof(FileParms, cookie) == 0x80 &&
                  offsetof(FileParms, dircallback) == 0x84 && sizeof(FileParms) == 0x90);
#endif

    /// The verb a FileParms names, read from a node the way PassThrough reads it.
    inline Verb VerbAt(const Filesystem* fs, uintptr_t offset)
    {
        return *reinterpret_cast<const Verb*>(reinterpret_cast<const char*>(fs) + offset);
    }
} // namespace wxl::core::filestack
