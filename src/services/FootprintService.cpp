// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WarcraftXL
//
// The wxl.footprint service: Storm's allocations by source file, the client's big consumers and
// the address space.

#include "services/FootprintService.hpp"

#include "binding/Binding.hpp"
#include "common/Log.hpp"
#include "runtime/Core.hpp"

#include "wxl/host/mod_host.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace
{
    constexpr char kTag[] = "footprint";

    // --- the accounting --------------------------------------------------------------------
    //
    // Two tables. One holds a row per source file Storm was told, keyed by the address of the
    // file's name -- the client passes string literals, so one file is one address -- with its
    // text copied at first sight and counters updated without a lock. The other holds every
    // block handed out while accounting runs, with its size and its source, so that a free knows
    // what to take back: 64 shards by address, each an open-addressed table behind its own lock,
    // so that threads allocating at once rarely meet. Neither allocates through Storm, or through
    // anything that could reach it: the blocks table lives in pages of its own.

    std::atomic<bool> g_on{false};
    bool              g_ran = false; ///< accounting ran at least once: the `storm` counts mean something

    constexpr uint32_t  kSourceSlots = 4096;
    constexpr uint32_t  kSourceLimit = 3072; ///< past this, new files share one row
    constexpr uint32_t  kOverflow    = kSourceSlots;
    constexpr uint32_t  kTextMax     = 112;
    constexpr uintptr_t kNoFile      = 1; ///< the key of an allocation that named no file

    struct Source
    {
        std::atomic<uintptr_t> key{0};
        char                   text[kTextMax]{};
        std::atomic<uintptr_t> bytes{0};
        std::atomic<uintptr_t> count{0};
        std::atomic<uintptr_t> peak{0};
        std::atomic<uint64_t>  allocations{0};
    };

    /// The source rows, and one more that every file past kSourceLimit shares.
    Source             g_sources[kSourceSlots + 1];
    uint32_t           g_sourceCount = 0;
    SRWLOCK            g_sourceLock  = SRWLOCK_INIT;

    /// Every source together: what `storm` shows.
    std::atomic<uintptr_t> g_totalBytes{0};
    std::atomic<uintptr_t> g_totalCount{0};
    std::atomic<uintptr_t> g_totalPeak{0};

    /// A pointer's bits spread over 32: murmur3's finalizer over its 8-byte units.
    uint32_t Mix(uintptr_t value)
    {
        uint64_t x = static_cast<uint64_t>(value) >> 3;
        uint32_t h = static_cast<uint32_t>(x) ^ static_cast<uint32_t>(x >> 32);
        h ^= h >> 16;
        h *= 0x85EBCA6Bu;
        h ^= h >> 13;
        h *= 0xC2B2AE35u;
        h ^= h >> 16;
        return h;
    }

    void Raise(std::atomic<uintptr_t>& peak, uintptr_t now)
    {
        uintptr_t seen = peak.load(std::memory_order_relaxed);
        while (now > seen && !peak.compare_exchange_weak(seen, now, std::memory_order_relaxed))
        {
        }
    }

    void Add(uint32_t source, uint32_t size)
    {
        Source&         s   = g_sources[source];
        const uintptr_t now = s.bytes.fetch_add(size, std::memory_order_relaxed) + size;
        s.count.fetch_add(1, std::memory_order_relaxed);
        s.allocations.fetch_add(1, std::memory_order_relaxed);
        Raise(s.peak, now);
        const uintptr_t total = g_totalBytes.fetch_add(size, std::memory_order_relaxed) + size;
        g_totalCount.fetch_add(1, std::memory_order_relaxed);
        Raise(g_totalPeak, total);
    }

    void Sub(uint32_t source, uint32_t size)
    {
        Source& s = g_sources[source];
        s.bytes.fetch_sub(size, std::memory_order_relaxed);
        s.count.fetch_sub(1, std::memory_order_relaxed);
        g_totalBytes.fetch_sub(size, std::memory_order_relaxed);
        g_totalCount.fetch_sub(1, std::memory_order_relaxed);
    }

    /// A file's name as a row keeps it: whole when it fits, else its end, which names the file.
    void CopyText(char (&out)[kTextMax], const char* file)
    {
        if (!file)
        {
            std::memcpy(out, "(no file)", sizeof "(no file)");
            return;
        }
        size_t length = 0;
        while (length < 1024 && file[length]) ++length;
        if (length < kTextMax)
        {
            std::memcpy(out, file, length);
            out[length] = 0;
            return;
        }
        const size_t keep = kTextMax - 4;
        std::memcpy(out, "...", 3);
        std::memcpy(out + 3, file + length - keep, keep);
        out[kTextMax - 1] = 0;
    }

    /// The row of a file, made at its first allocation.
    uint32_t SourceOf(const char* file)
    {
        const uintptr_t key  = file ? reinterpret_cast<uintptr_t>(file) : kNoFile;
        const uint32_t  home = Mix(key) & (kSourceSlots - 1);
        for (uint32_t i = 0; i < kSourceSlots; ++i)
        {
            const uint32_t  at   = (home + i) & (kSourceSlots - 1);
            const uintptr_t seen = g_sources[at].key.load(std::memory_order_acquire);
            if (seen == key) return at;
            if (seen != 0) continue;

            AcquireSRWLockExclusive(&g_sourceLock);
            uint32_t found = kOverflow;
            for (uint32_t j = i; j < kSourceSlots; ++j)
            {
                const uint32_t  slot  = (home + j) & (kSourceSlots - 1);
                const uintptr_t taken = g_sources[slot].key.load(std::memory_order_relaxed);
                if (taken == key)
                {
                    found = slot;
                    break;
                }
                if (taken != 0) continue;
                if (g_sourceCount >= kSourceLimit) break;
                CopyText(g_sources[slot].text, file);
                g_sources[slot].key.store(key, std::memory_order_release);
                ++g_sourceCount;
                found = slot;
                break;
            }
            ReleaseSRWLockExclusive(&g_sourceLock);
            return found;
        }
        return kOverflow;
    }

    struct Entry
    {
        uintptr_t block;  ///< 0: an empty slot
        uint32_t  size;
        uint32_t  source;
    };

    struct Shard
    {
        SRWLOCK  lock     = SRWLOCK_INIT;
        Entry*   slots    = nullptr;
        uint32_t capacity = 0; ///< a power of two, or 0 before the first block
        uint32_t used     = 0;
    };

    constexpr uint32_t kShards       = 64;
    constexpr uint32_t kFirstSlots   = 1024;
    Shard              g_shards[kShards];
    std::atomic<uint64_t> g_mapBytes{0};

    Shard&   ShardOf(uint32_t mixed) { return g_shards[mixed & (kShards - 1)]; }
    uint32_t HomeOf(uint32_t mixed, uint32_t capacity) { return (mixed >> 6) & (capacity - 1); }

    Entry* Pages(uint32_t slots)
    {
        return static_cast<Entry*>(
            VirtualAlloc(nullptr, sizeof(Entry) * slots, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    }

    /// Doubles a shard's table (or makes its first): false when no pages are left for it.
    bool Grow(Shard& shard)
    {
        const uint32_t capacity = shard.capacity ? shard.capacity * 2 : kFirstSlots;
        Entry* const   slots    = Pages(capacity);
        if (!slots) return false;
        for (uint32_t i = 0; i < shard.capacity; ++i)
        {
            const Entry& entry = shard.slots[i];
            if (!entry.block) continue;
            uint32_t at = HomeOf(Mix(entry.block), capacity);
            while (slots[at].block) at = (at + 1) & (capacity - 1);
            slots[at] = entry;
        }
        if (shard.slots) VirtualFree(shard.slots, 0, MEM_RELEASE);
        g_mapBytes.fetch_add(sizeof(Entry) * (capacity - shard.capacity), std::memory_order_relaxed);
        shard.slots    = slots;
        shard.capacity = capacity;
        return true;
    }

    /// Keeps a block. One already kept was freed where no seam saw it: what it held leaves the
    /// counts. False when the block is not kept: accounting stopped, or no page was left.
    bool Keep(uintptr_t block, uint32_t size, uint32_t source)
    {
        const uint32_t mixed = Mix(block);
        Shard&         shard = ShardOf(mixed);
        bool           kept  = false;
        bool           had   = false;
        Entry          stale{};
        AcquireSRWLockExclusive(&shard.lock);
        if (g_on.load(std::memory_order_relaxed) &&
            ((shard.used + 1) * 4 <= shard.capacity * 3 || Grow(shard)))
        {
            uint32_t at = HomeOf(mixed, shard.capacity);
            while (shard.slots[at].block && shard.slots[at].block != block) at = (at + 1) & (shard.capacity - 1);
            if (shard.slots[at].block)
            {
                stale = shard.slots[at];
                had   = true;
            }
            else
                ++shard.used;
            shard.slots[at] = Entry{block, size, source};
            kept            = true;
        }
        ReleaseSRWLockExclusive(&shard.lock);
        if (had) Sub(stale.source, stale.size);
        return kept;
    }

    /// Takes a block out, with what it held; false when it was not kept.
    bool Drop(uintptr_t block, Entry* out)
    {
        const uint32_t mixed = Mix(block);
        Shard&         shard = ShardOf(mixed);
        bool           found = false;
        AcquireSRWLockExclusive(&shard.lock);
        if (shard.capacity)
        {
            const uint32_t mask = shard.capacity - 1;
            uint32_t       at   = HomeOf(mixed, shard.capacity);
            while (shard.slots[at].block && shard.slots[at].block != block) at = (at + 1) & mask;
            if (shard.slots[at].block)
            {
                *out  = shard.slots[at];
                found = true;
                --shard.used;
                // Backward shift: each entry after the hole that would no longer be reached from
                // its home moves into the hole, so a probe never stops at an empty slot early.
                uint32_t hole = at;
                for (uint32_t next = (hole + 1) & mask; shard.slots[next].block; next = (next + 1) & mask)
                {
                    const uint32_t home = HomeOf(Mix(shard.slots[next].block), shard.capacity);
                    const bool     stays =
                        hole <= next ? (home > hole && home <= next) : (home > hole || home <= next);
                    if (stays) continue;
                    shard.slots[hole] = shard.slots[next];
                    hole              = next;
                }
                shard.slots[hole] = Entry{};
            }
        }
        ReleaseSRWLockExclusive(&shard.lock);
        return found;
    }

    bool Find(uintptr_t block, Entry* out)
    {
        const uint32_t mixed = Mix(block);
        Shard&         shard = ShardOf(mixed);
        bool           found = false;
        AcquireSRWLockShared(&shard.lock);
        if (shard.capacity)
        {
            uint32_t at = HomeOf(mixed, shard.capacity);
            while (shard.slots[at].block && shard.slots[at].block != block) at = (at + 1) & (shard.capacity - 1);
            if (shard.slots[at].block)
            {
                *out  = shard.slots[at];
                found = true;
            }
        }
        ReleaseSRWLockShared(&shard.lock);
        return found;
    }

    void Account(uintptr_t block, uint32_t size, uint32_t source)
    {
        if (Keep(block, size, source)) Add(source, size);
    }

    uint32_t Rounded(size_t bytes)
    {
        const uint64_t size = (static_cast<uint64_t>(bytes) + 7) & ~static_cast<uint64_t>(7);
        return size > 0xFFFFFFF8u ? 0xFFFFFFF8u : static_cast<uint32_t>(size);
    }

    /// The block SMemReAlloc was entered with, held between its two seams on this thread.
    struct Moving
    {
        uintptr_t block = 0;
        Entry     held{};
    };
    thread_local Moving t_moving;

    void FreeTables()
    {
        for (Shard& shard : g_shards)
        {
            AcquireSRWLockExclusive(&shard.lock);
            if (shard.slots) VirtualFree(shard.slots, 0, MEM_RELEASE);
            shard.slots    = nullptr;
            shard.capacity = 0;
            shard.used     = 0;
            ReleaseSRWLockExclusive(&shard.lock);
        }
        g_mapBytes.store(0, std::memory_order_relaxed);
    }

    void ZeroCounts()
    {
        for (Source& source : g_sources)
        {
            source.bytes.store(0, std::memory_order_relaxed);
            source.count.store(0, std::memory_order_relaxed);
            source.peak.store(0, std::memory_order_relaxed);
            source.allocations.store(0, std::memory_order_relaxed);
        }
        g_totalBytes.store(0, std::memory_order_relaxed);
        g_totalCount.store(0, std::memory_order_relaxed);
        g_totalPeak.store(0, std::memory_order_relaxed);
    }

    // --- what mods add --------------------------------------------------------------------

    struct Report
    {
        uint32_t    mod;
        std::string name;
        uint64_t    bytes;
        uint64_t    count;
    };

    struct Threshold
    {
        uint32_t mod;
        uint64_t bytes;
    };

    SRWLOCK                g_modLock = SRWLOCK_INIT;
    std::vector<Report>    g_reports;
    std::vector<Threshold> g_thresholds;
    bool                   g_under    = false; ///< the last look found the largest block under it
    ULONGLONG              g_lastLook = 0;

    uint32_t Caller()
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        return host ? host->CurrentMod() : 0u;
    }

    std::string OwnerOf(uint32_t mod)
    {
        const wxl::host::ModHost* host = wxl::core::Host();
        const wxl::host::LoadedMod* loaded = host ? host->ModOf(mod) : nullptr;
        if (loaded) return loaded->id;
        return mod == 0 ? std::string("runtime") : std::string("mod #") + std::to_string(mod);
    }

    // --- the address space ----------------------------------------------------------------

    struct Space
    {
        uint64_t bytes[3][3]{};   ///< [committed, reserved, free][image, mapped, private]
        uint32_t regions[3][3]{};
        uint64_t largest[3][3]{};
    };

    Space Measure()
    {
        Space       space;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        uintptr_t       at  = reinterpret_cast<uintptr_t>(info.lpMinimumApplicationAddress);
        const uintptr_t end = reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress);
        while (at < end)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (!VirtualQuery(reinterpret_cast<const void*>(at), &region, sizeof region)) break;
            const uint64_t size  = region.RegionSize;
            const int      state = region.State == MEM_COMMIT ? 0 : region.State == MEM_RESERVE ? 1 : 2;
            const int      type  = state == 2 ? 0 : region.Type == MEM_IMAGE ? 0 : region.Type == MEM_MAPPED ? 1 : 2;
            space.bytes[state][type] += size;
            space.regions[state][type] += 1;
            space.largest[state][type] = std::max(space.largest[state][type], size);
            const uintptr_t next = reinterpret_cast<uintptr_t>(region.BaseAddress) + region.RegionSize;
            if (next <= at) break;
            at = next;
        }
        return space;
    }

    // --- the picture ----------------------------------------------------------------------

    struct CategoryRow
    {
        std::string name, owner;
        uint64_t    bytes = 0, count = 0, peak = 0;
    };
    struct SourceRow
    {
        std::string file;
        uint64_t    bytes = 0, count = 0, peak = 0, allocations = 0;
    };
    struct ArchiveRow
    {
        std::string path;
        int32_t     priority = 0;
        uint32_t    hash = 0, block = 0;
        uint64_t    bytes = 0;
    };
    struct TableRow
    {
        std::string name;
        uint32_t    records = 0, record_size = 0;
        uint64_t    bytes = 0;
    };
    struct RegionRow
    {
        std::string state, type;
        uint64_t    bytes = 0;
        uint32_t    regions = 0;
        uint64_t    largest = 0;
    };

    struct Picture
    {
        std::vector<CategoryRow> categories;
        std::vector<SourceRow>   sources;
        std::vector<ArchiveRow>  archives;
        std::vector<TableRow>    tables;
        std::vector<RegionRow>   regions;
    };

    SRWLOCK g_pictureLock = SRWLOCK_INIT;
    Picture g_picture;

    wxl_str StrOf(const std::string& text) { return wxl_str{text.c_str(), static_cast<uint32_t>(text.size())}; }

    struct Gpu
    {
        uint64_t bytes[2][4]{}; ///< [texture, buffer][D3DPOOL]
        uint64_t count[2][4]{};
    };

    void WXL_CALL OnGpu(const wxl_core_gpu_resource* resource, void* user)
    {
        Gpu& gpu = *static_cast<Gpu*>(user);
        if (!resource || resource->kind > 1) return;
        const uint32_t pool = resource->pool < 4 ? resource->pool : 3;
        gpu.bytes[resource->kind][pool] += resource->bytes;
        gpu.count[resource->kind][pool] += 1;
    }

    void WXL_CALL OnArchive(const wxl_core_archive* archive, void* user)
    {
        if (!archive) return;
        static_cast<std::vector<ArchiveRow>*>(user)->push_back(
            ArchiveRow{archive->path ? archive->path : "", archive->priority, archive->hash_entries,
                       archive->block_entries, archive->bytes});
    }

    void WXL_CALL OnDb(const wxl_core_db* db, void* user)
    {
        if (!db || !db->loaded) return;
        TableRow row{db->file ? db->file : "", db->records, db->record_size, 0};
        for (uint32_t i = 0; i < WXL_CORE_DB_BLOCKS; ++i)
        {
            uint64_t size = 0;
            if (db->blocks[i] && wxl::core::services::footprint::BlockSize(db->blocks[i], &size))
                row.bytes += size;
            else if (db->blocks[i])
                row.bytes += db->estimates[i];
        }
        static_cast<std::vector<TableRow>*>(user)->push_back(std::move(row));
    }

    void Take(Picture& picture)
    {
        // Storm, by source file: rows sharing a name's text are one file.
        std::map<std::string, SourceRow> files;
        for (uint32_t i = 0; i <= kSourceSlots; ++i)
        {
            const Source& source = g_sources[i];
            const bool    used   = i == kOverflow ? source.allocations.load(std::memory_order_relaxed) != 0
                                                  : source.key.load(std::memory_order_acquire) != 0;
            if (!used) continue;
            const std::string name = i == kOverflow ? std::string("(other files)") : std::string(source.text);
            SourceRow&        row  = files[name];
            row.file = name;
            row.bytes += source.bytes.load(std::memory_order_relaxed);
            row.count += source.count.load(std::memory_order_relaxed);
            row.peak += source.peak.load(std::memory_order_relaxed);
            row.allocations += source.allocations.load(std::memory_order_relaxed);
        }
        for (auto& [name, row] : files)
            if (row.allocations) picture.sources.push_back(std::move(row));
        std::sort(picture.sources.begin(), picture.sources.end(), [](const SourceRow& a, const SourceRow& b) {
            return a.bytes != b.bytes ? a.bytes > b.bytes : a.file < b.file;
        });

        auto category = [&](const char* name, uint64_t bytes, uint64_t count, uint64_t peak) {
            picture.categories.push_back(CategoryRow{name, "client", bytes, count, peak});
        };
        if (g_ran)
            category("storm", g_totalBytes.load(std::memory_order_relaxed),
                     g_totalCount.load(std::memory_order_relaxed),
                     g_totalPeak.load(std::memory_order_relaxed));

        Gpu gpu;
        wxl::core::binding::gx_resources(&OnGpu, &gpu);
        static const char* const kPools[2][4] = {
            {"textures.default", "textures.managed", "textures.systemmem", "textures.scratch"},
            {"buffers.default", "buffers.managed", "buffers.systemmem", "buffers.scratch"},
        };
        for (int kind = 0; kind < 2; ++kind)
            for (int pool = 0; pool < 4; ++pool)
                if (gpu.count[kind][pool])
                    category(kPools[kind][pool], gpu.bytes[kind][pool], gpu.count[kind][pool], gpu.bytes[kind][pool]);

        uint64_t soundNow = 0, soundPeak = 0;
        if (wxl::core::binding::sound_heap(&soundNow, &soundPeak)) category("sound", soundNow, 0, soundPeak);
        if (const uint64_t script = wxl::core::binding::script_heap()) category("script", script, 0, script);

        wxl::core::binding::archive_tables(&OnArchive, &picture.archives);
        if (!picture.archives.empty())
        {
            uint64_t bytes = 0;
            for (const ArchiveRow& row : picture.archives) bytes += row.bytes;
            category("archives", bytes, picture.archives.size(), bytes);
        }

        wxl::core::binding::client_dbs(&OnDb, &picture.tables);
        std::sort(picture.tables.begin(), picture.tables.end(),
                  [](const TableRow& a, const TableRow& b) { return a.name < b.name; });
        if (!picture.tables.empty())
        {
            uint64_t bytes = 0;
            for (const TableRow& row : picture.tables) bytes += row.bytes;
            category("tables", bytes, picture.tables.size(), bytes);
        }

        const uint64_t accounting = g_mapBytes.load(std::memory_order_relaxed);
        if (accounting || g_ran) category("accounting", accounting + sizeof g_sources, 0, accounting + sizeof g_sources);

        AcquireSRWLockShared(&g_modLock);
        for (const Report& report : g_reports)
            picture.categories.push_back(CategoryRow{report.name, OwnerOf(report.mod), report.bytes, report.count,
                                                     report.bytes});
        ReleaseSRWLockShared(&g_modLock);

        std::stable_sort(picture.categories.begin(), picture.categories.end(),
                         [](const CategoryRow& a, const CategoryRow& b) { return a.bytes > b.bytes; });

        const Space          space    = Measure();
        static const char*   kStates[] = {"committed", "reserved", "free"};
        static const char*   kTypes[]  = {"image", "mapped", "private"};
        for (int state = 0; state < 3; ++state)
            for (int type = 0; type < (state == 2 ? 1 : 3); ++type)
                picture.regions.push_back(RegionRow{kStates[state], state == 2 ? "" : kTypes[type],
                                                    space.bytes[state][type], space.regions[state][type],
                                                    space.largest[state][type]});
    }

    // --- the table ------------------------------------------------------------------------

    wxl_result WXL_CALL Start()
    {
        g_on.store(false, std::memory_order_relaxed);
        for (Shard& shard : g_shards)
        {
            AcquireSRWLockExclusive(&shard.lock);
            if (shard.slots) std::memset(shard.slots, 0, sizeof(Entry) * shard.capacity);
            shard.used = 0;
            ReleaseSRWLockExclusive(&shard.lock);
        }
        ZeroCounts();
        g_ran = true;
        g_on.store(true, std::memory_order_release);
        WLOG_INFO(kTag, "accounting Storm's allocations from now on");
        return WXL_OK;
    }

    void WXL_CALL Stop()
    {
        if (!g_on.exchange(false)) return;
        FreeTables();
        WLOG_INFO(kTag, "accounting stopped: %llu byte(s) in %llu block(s) were live",
                  static_cast<unsigned long long>(g_totalBytes.load()),
                  static_cast<unsigned long long>(g_totalCount.load()));
    }

    wxl_bool WXL_CALL IsRunning() { return g_on.load(std::memory_order_relaxed) ? WXL_TRUE : WXL_FALSE; }

    wxl_result WXL_CALL Snapshot()
    {
        Picture picture;
        Take(picture);
        AcquireSRWLockExclusive(&g_pictureLock);
        g_picture = std::move(picture);
        ReleaseSRWLockExclusive(&g_pictureLock);
        return WXL_OK;
    }

    /// Counted, or filled as far as `capacity`: one list of the last picture.
    template <class Row, class C, class F>
    uint32_t Fill(const std::vector<Row>& rows, C* out, uint32_t capacity, F&& convert)
    {
        for (uint32_t i = 0; out && i < capacity && i < rows.size(); ++i)
        {
            const uint32_t size = out[i].struct_size ? out[i].struct_size : static_cast<uint32_t>(sizeof(C));
            C              entry{};
            convert(rows[i], entry);
            entry.struct_size = static_cast<uint32_t>(sizeof(C));
            std::memcpy(&out[i], &entry, size < sizeof(C) ? size : sizeof(C));
        }
        return static_cast<uint32_t>(rows.size());
    }

    uint32_t WXL_CALL Categories(wxl_footprint_category* out, uint32_t capacity)
    {
        AcquireSRWLockShared(&g_pictureLock);
        const uint32_t n = Fill(g_picture.categories, out, capacity, [](const CategoryRow& r, wxl_footprint_category& c) {
            c.name  = StrOf(r.name);
            c.owner = StrOf(r.owner);
            c.bytes = r.bytes;
            c.count = r.count;
            c.peak  = r.peak;
        });
        ReleaseSRWLockShared(&g_pictureLock);
        return n;
    }

    uint32_t WXL_CALL Sources(wxl_footprint_source* out, uint32_t capacity)
    {
        AcquireSRWLockShared(&g_pictureLock);
        const uint32_t n = Fill(g_picture.sources, out, capacity, [](const SourceRow& r, wxl_footprint_source& s) {
            s.file        = StrOf(r.file);
            s.bytes       = r.bytes;
            s.count       = r.count;
            s.peak        = r.peak;
            s.allocations = r.allocations;
        });
        ReleaseSRWLockShared(&g_pictureLock);
        return n;
    }

    uint32_t WXL_CALL Archives(wxl_footprint_archive* out, uint32_t capacity)
    {
        AcquireSRWLockShared(&g_pictureLock);
        const uint32_t n = Fill(g_picture.archives, out, capacity, [](const ArchiveRow& r, wxl_footprint_archive& a) {
            a.path          = StrOf(r.path);
            a.priority      = r.priority;
            a.hash_entries  = r.hash;
            a.block_entries = r.block;
            a.bytes         = r.bytes;
        });
        ReleaseSRWLockShared(&g_pictureLock);
        return n;
    }

    uint32_t WXL_CALL Tables(wxl_footprint_table* out, uint32_t capacity)
    {
        AcquireSRWLockShared(&g_pictureLock);
        const uint32_t n = Fill(g_picture.tables, out, capacity, [](const TableRow& r, wxl_footprint_table& t) {
            t.name        = StrOf(r.name);
            t.records     = r.records;
            t.record_size = r.record_size;
            t.bytes       = r.bytes;
        });
        ReleaseSRWLockShared(&g_pictureLock);
        return n;
    }

    uint32_t WXL_CALL Regions(wxl_footprint_region* out, uint32_t capacity)
    {
        AcquireSRWLockShared(&g_pictureLock);
        const uint32_t n = Fill(g_picture.regions, out, capacity, [](const RegionRow& r, wxl_footprint_region& g) {
            g.state   = StrOf(r.state);
            g.type    = StrOf(r.type);
            g.bytes   = r.bytes;
            g.regions = r.regions;
            g.largest = r.largest;
        });
        ReleaseSRWLockShared(&g_pictureLock);
        return n;
    }

    wxl_result WXL_CALL ReportOwn(wxl_str name, uint64_t bytes, uint64_t count)
    {
        if (!name.data || !name.size) return WXL_E_INVALID;
        const uint32_t    mod = Caller();
        const std::string text(name.data, name.size);
        AcquireSRWLockExclusive(&g_modLock);
        auto known = std::find_if(g_reports.begin(), g_reports.end(),
                                  [&](const Report& r) { return r.mod == mod && r.name == text; });
        if (bytes == 0 && count == 0)
        {
            if (known != g_reports.end()) g_reports.erase(known);
        }
        else if (known != g_reports.end())
        {
            known->bytes = bytes;
            known->count = count;
        }
        else
            g_reports.push_back(Report{mod, text, bytes, count});
        ReleaseSRWLockExclusive(&g_modLock);
        return WXL_OK;
    }

    wxl_result WXL_CALL WatchPressure(uint64_t bytes)
    {
        const uint32_t mod = Caller();
        AcquireSRWLockExclusive(&g_modLock);
        g_thresholds.erase(std::remove_if(g_thresholds.begin(), g_thresholds.end(),
                                          [&](const Threshold& t) { return t.mod == mod; }),
                           g_thresholds.end());
        if (bytes) g_thresholds.push_back(Threshold{mod, bytes});
        g_under    = false;
        g_lastLook = 0;
        ReleaseSRWLockExclusive(&g_modLock);
        return WXL_OK;
    }

    const wxl_footprint_api g_footprint = {
        sizeof(wxl_footprint_api),
        &Start,
        &Stop,
        &IsRunning,
        &Snapshot,
        &Categories,
        &Sources,
        &Archives,
        &Tables,
        &Regions,
        &ReportOwn,
        &WatchPressure,
    };
}

namespace wxl::core::services
{
    const wxl_footprint_api& FootprintApi() { return g_footprint; }

    namespace footprint
    {
        void Allocated(void* block, size_t bytes, const char* file)
        {
            if (!g_on.load(std::memory_order_relaxed) || !block) return;
            Account(reinterpret_cast<uintptr_t>(block), Rounded(bytes), SourceOf(file));
        }

        void Freeing(void* block)
        {
            if (!g_on.load(std::memory_order_relaxed) || !block) return;
            Entry held{};
            if (Drop(reinterpret_cast<uintptr_t>(block), &held)) Sub(held.source, held.size);
        }

        void Reallocating(void* block, uint32_t flags)
        {
            t_moving.block = 0;
            // SMEM_REALLOC_NULL answers null without touching anything, SMEM_FLAG_NOREALLOC null
            // too, and no block is SMemAlloc's call, which its own seam counts.
            if (!g_on.load(std::memory_order_relaxed) || !block || flags == 0xB00BEEE5u || (flags & 0x10u)) return;
            Entry held{};
            if (!Drop(reinterpret_cast<uintptr_t>(block), &held)) return;
            Sub(held.source, held.size);
            t_moving.block = reinterpret_cast<uintptr_t>(block);
            t_moving.held  = held;
        }

        void Reallocated(void* result, void* block, size_t bytes, const char* file)
        {
            const Moving moving = t_moving;
            t_moving.block      = 0;
            if (!moving.block || moving.block != reinterpret_cast<uintptr_t>(block)) return;
            if (result)
                Allocated(result, bytes, file);
            else if (Rounded(bytes) != 0 && g_on.load(std::memory_order_relaxed))
                // The call failed and the block is still there, as it was.
                Account(moving.block, moving.held.size, moving.held.source);
        }

        bool Running() { return g_on.load(std::memory_order_relaxed); }

        bool BlockSize(const void* block, uint64_t* size)
        {
            Entry held{};
            if (!block || !size || !Find(reinterpret_cast<uintptr_t>(block), &held)) return false;
            *size = held.size;
            return true;
        }

        bool PressureDue(wxl_memory_pressure_args* out)
        {
            AcquireSRWLockShared(&g_modLock);
            uint64_t threshold = 0;
            for (const Threshold& t : g_thresholds) threshold = std::max(threshold, t.bytes);
            ReleaseSRWLockShared(&g_modLock);
            if (!threshold || !out) return false;

            const ULONGLONG now = GetTickCount64();
            if (g_lastLook && now - g_lastLook < 1000) return false;
            g_lastLook = now;

            const Space space = Measure();
            const uint64_t largest   = space.largest[2][0];
            if (largest >= threshold)
            {
                g_under = false;
                return false;
            }
            if (g_under) return false;
            g_under = true;

            *out              = wxl_memory_pressure_args{};
            out->largest_free = largest;
            out->free         = space.bytes[2][0];
            out->committed    = space.bytes[0][0] + space.bytes[0][1] + space.bytes[0][2];
            out->threshold    = threshold;
            WLOG_WARN(kTag, "address space low: the largest free block is %llu KB, under %llu KB",
                      static_cast<unsigned long long>(largest / 1024),
                      static_cast<unsigned long long>(threshold / 1024));
            return true;
        }

        void ForgetMod(uint32_t mod)
        {
            AcquireSRWLockExclusive(&g_modLock);
            g_reports.erase(std::remove_if(g_reports.begin(), g_reports.end(),
                                           [&](const Report& r) { return r.mod == mod; }),
                            g_reports.end());
            g_thresholds.erase(std::remove_if(g_thresholds.begin(), g_thresholds.end(),
                                              [&](const Threshold& t) { return t.mod == mod; }),
                               g_thresholds.end());
            ReleaseSRWLockExclusive(&g_modLock);
        }

        void Reset()
        {
            g_on.store(false);
            FreeTables();
            ZeroCounts();
            g_ran = false;
            AcquireSRWLockExclusive(&g_modLock);
            g_reports.clear();
            g_thresholds.clear();
            g_under    = false;
            g_lastLook = 0;
            ReleaseSRWLockExclusive(&g_modLock);
            AcquireSRWLockExclusive(&g_pictureLock);
            g_picture = Picture{};
            ReleaseSRWLockExclusive(&g_pictureLock);
        }
    }
}
