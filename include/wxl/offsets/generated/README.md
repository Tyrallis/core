# Generated offset tables

**Nothing in this directory is written by hand.** Every file is produced from
[`WarcraftXL/client`](https://github.com/WarcraftXL/client), the decompiled fork, where each
reconstructed function and static carries its address in the original `Wow.exe` 3.3.5a (12340) as
a source tag, and where the struct layouts can be measured by compiling 32-bit.

| File | Produced by | From |
|---|---|---|
| `functions.def` | `tools/offsets/extract_offsets.py` | the fork's `// OFFSET:` tags |
| `statics.def` | the same | the fork's `// STATIC:` tags |
| `layouts.def` | `tools/layouts/measure_layouts.py` | a 32-bit `offsetof` dump of the fork's headers |

Re-run both after pulling upstream. Each tool is deterministic: two runs give byte-identical
output, so the diff of a regeneration is exactly what upstream changed and nothing else. Their
reports land in `core/reports/`, which is not tracked — they are reproducible in seconds.

## Shards

`functions.def` and `statics.def` are aggregates: they contain nothing but `#include` lines for
the per-module shards under `functions/` and `statics/`, split to mirror the fork's own source
tree (`engine/gx`, `wow/object`, ...). A shard is never included directly; include the aggregate
with your macro defined and you get every row.

The split exists for the diff. A pull that touches only the renderer changes only
`functions/engine/gx.def`, which is reviewable; one file of 26,000 rows would not be.

## Consuming them

These are data, included from **one** translation unit with the X-macro defined, never from a
widely included header. The column contract is documented at the top of each aggregate.

To refer to a single address from hand-written code, do not copy the literal out: use
`../site.hpp`, which resolves a name at compile time and fails the build when the symbol is gone
or a delta reaches past the end of its function.

```cpp
#include "wxl/offsets/site.hpp"

// Main thread only: the list head is read from thread-local storage.
constexpr uintptr_t kEnumObjects = WXL_SITE("ClntObjMgrEnumVisibleObjects", 0);
```

That is the whole point of generating these: an upstream rename becomes a compile error instead
of an address that is silently wrong months later.

## What these tables do not carry

The reverse-engineering knowledge around an address — that a function must be called on the main
thread, that a hook belongs before the call rather than after, which of two identical bodies the
linker folded. That lives in the hand-written layer that references these rows, and no generator
will ever produce it.
