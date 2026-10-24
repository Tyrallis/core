// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// A reader of a server message a seam was handed, for the bodies that read a message before
// the client parses it, on both builds.

#pragma once

#include "binding/Binding.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace wxl::core::seams
{
    /**
     * @brief Reads a message's body in order, from the store's cursor onwards.
     *
     * The store itself is never moved: what this reads is what the client's own handler is
     * about to read, and the handler must find the cursor where the dispatcher left it. A read
     * past the end fails and every read after it fails too, so a truncated message raises
     * nothing rather than something half read. A reader copies cheaply, which is how a body
     * checks a whole list before raising its first entry.
     */
    class MessageReader
    {
    public:
        explicit MessageReader(void* store)
        {
            if (!store) return;
            const uint8_t* data = static_cast<const uint8_t*>(binding::message_data(store));
            const uint32_t size = binding::message_size(store);
            const uint32_t read = binding::message_read(store);
            if (!data || read > size) return;
            at_  = data + read;
            end_ = data + size;
        }

        bool U8(uint8_t& out) { return Take(&out, sizeof out); }
        bool U16(uint16_t& out) { return Take(&out, sizeof out); }
        bool U32(uint32_t& out) { return Take(&out, sizeof out); }
        bool U64(uint64_t& out) { return Take(&out, sizeof out); }
        bool F32(float& out) { return Take(&out, sizeof out); }

        /// A NUL-terminated string in place: its first byte and its length without the NUL.
        /// Fails when no NUL is left in the message.
        bool CString(const char*& out, uint32_t& length)
        {
            if (!at_) return false;
            const uint8_t* nul = static_cast<const uint8_t*>(std::memchr(at_, 0, Left()));
            if (!nul)
            {
                at_ = nullptr;
                return false;
            }
            out    = reinterpret_cast<const char*>(at_);
            length = static_cast<uint32_t>(nul - at_);
            at_    = nul + 1;
            return true;
        }

        /// A string the client reads in situ: its length (the NUL counted) as a dword, then its
        /// bytes. Answers the text without the NUL; an empty string for a length of zero.
        bool SizedString(const char*& out, uint32_t& length)
        {
            uint32_t size = 0;
            if (!U32(size)) return false;
            const uint8_t* start = at_;
            if (!Skip(size)) return false;
            out    = reinterpret_cast<const char*>(start);
            length = size && start[size - 1] == 0 ? size - 1 : size;
            return true;
        }

        bool Skip(size_t count)
        {
            if (!at_ || static_cast<size_t>(end_ - at_) < count)
            {
                at_ = nullptr;
                return false;
            }
            at_ += count;
            return true;
        }

        /// A guid as the server packs it: a mask byte, then one byte per bit set, lowest first.
        bool PackedGuid(uint64_t& out)
        {
            uint8_t mask = 0;
            out         = 0;
            if (!U8(mask)) return false;
            for (uint32_t bit = 0; bit < 8; ++bit)
            {
                if (!(mask & (1u << bit))) continue;
                uint8_t byte = 0;
                if (!U8(byte)) return false;
                out |= static_cast<uint64_t>(byte) << (bit * 8);
            }
            return true;
        }

        /// Where the next read starts; null once a read failed.
        const uint8_t* At() const { return at_; }

        /// The bytes left to read; 0 once a read failed.
        size_t Left() const { return at_ ? static_cast<size_t>(end_ - at_) : 0; }

    private:
        bool Take(void* out, size_t count)
        {
            if (!Skip(count)) return false;
            std::memcpy(out, at_ - count, count);
            return true;
        }

        const uint8_t* at_  = nullptr;
        const uint8_t* end_ = nullptr;
    };
}
