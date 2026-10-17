# offsets: derived address tables

`extract_offsets.py` reads the `client` fork (the C++ decompilation of Wow.exe 3.3.5a, build
12340) and turns its address tags into the X-macro tables under `core/offsets/`. The fork is the
single source of truth for addresses; nothing in `core/offsets/` is written by hand.

The tags it reads sit directly above the thing they describe:

```cpp
// VERIFY: 271a03c4 manual
// OFFSET: 0x0068FA60
void CGxDeviceD3d::IBufUnlock(CGxBuf* buf) {

// STATIC: 0x00C60930
static BLIT_FUNCTION s_blits[BlitFormats_Last][BlitFormats_Last][BlitAlphas_Last];

// OFFSET: [0x0077A2B0<SpellRec>, 0x0077A310<ItemDisplayInfoRec>, 0x0077A370]
template <class T>
void WowClientDB<T>::Load() {
```

## Outputs

| file | content |
| --- | --- |
| `functions.def` | `WXL_OFFSET_FUNCTION(name, address, verify, targs, file, line)`, one row per function address; a template tag with N addresses gives N rows |
| `statics.def` | `WXL_OFFSET_STATIC(name, address, file, line)`, one row per static address |
| `unparsed.txt` | every `OFFSET:`/`STATIC:` tag the parser could not confidently name, with file, line, reason and the source line |
| `summary.txt` | counts per tag kind and per tree, headers versus sources, the reconciliation, VERIFY manual/auto counts, template histogram, duplicate addresses, names with several addresses, INLINED tags |

Names are the qualified C++ names as written in the fork, with enclosing namespaces and classes
applied (`FMOD::DiskFile::DiskFile` for an in-class definition, `CmdLineGetString::buffer` for a
function-local static). Text columns are C string literals; paths are relative to the client
checkout with forward slashes. Rows are sorted by address, then name, file and line, so two runs
on the same input give byte-identical files and a run after an upstream pull gives a reviewable
diff.

## Running it

```
python core/tools/offsets/extract_offsets.py <path to the client checkout>
```

Python 3, standard library only. The output directory defaults to `core/offsets/`; `--out <dir>`
writes elsewhere (useful to compare against the committed tables). The client checkout is only
read. The summary is printed; `--quiet` silences it.

## When to re-run

After every pull of the fork: new functions get tagged, addresses get corrected, names get
renamed. Re-run, read `summary.txt` and the diff of the two `.def` files, and commit the three
files together with the tool version that produced them.

## The invariant

Every `OFFSET:` and `STATIC:` tag ends up either as rows or as an entry in `unparsed.txt`, and
the tool refuses to write anything if the totals do not reconcile. A table that silently omitted
what it did not understand would be worse than no table, because nobody would ever find the hole.

## What to do with unparsed.txt

The file should be empty (header lines only). If an entry appears after a pull, the fork grew a
shape the parser does not know. Open the file and line, see what the declaration looks like, and
either teach `parse_function_signature` / `parse_static_declaration` the shape or fix the source
in the fork. Do not hand-edit the `.def` files around it: the next run would undo that.

## How core consumes the tables

The `.def` files are data, not interfaces. One translation unit in `core` defines
`WXL_OFFSET_FUNCTION` / `WXL_OFFSET_STATIC` to append to a lookup table (name to address and
address to name), includes the two files once, and exposes a lookup API; nothing else includes
them. They are 26k and 5k rows, so including them from a header would be paid in every
translation unit. That consumer is not part of this tool.

## Out of scope

Struct field layouts. Those come from compiling the fork's headers 32-bit and dumping `offsetof`,
a different mechanism.
