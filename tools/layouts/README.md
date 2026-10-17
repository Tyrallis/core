# layouts: measured struct layouts

`measure_layouts.py` compiles the `client` fork's own headers 32-bit and prints `sizeof` and
`offsetof` for a declared set of types and members. The result is the X-macro table
`core/offsets/layouts.def`, the second half of the derived tables next to `functions.def` and
`statics.def` (see `core/tools/offsets/`): addresses come from the fork's tags, layouts come from
the fork's headers under the compiler, and nothing in `core/offsets/` is written by hand.

The fork's root `CMakeLists.txt` keeps Win32 buildable for exactly this purpose: it is the
configuration whose struct layouts can be checked against Wow.exe 3.3.5a (12340), compiled with
`/arch:IA32 /fp:fast` and `_USE_32BIT_TIME_T` like the VS2005 original.

## Inputs

| file | role |
| --- | --- |
| `layouts.txt` | the declared, human-edited list of types and members to measure, and the hand constant(s) each one is compared with; the syntax is at the top of the file |
| the client checkout | read-only; its headers are included, its root `CMakeLists.txt` is read for the flags |
| `core/offsets/audit-handwritten.md` | the "Field-offset inventory" section: the 804 hand-written `constexpr size_t` constants of `client/warcraftxl/include/wxl/offsets`, with their values and comments |

## Outputs (in `core/offsets/`)

| file | content |
| --- | --- |
| `layouts.def` | `WXL_LAYOUT_TYPE(id, type, size, file, line)`, `WXL_LAYOUT_FIELD(id, member, offset, file, line)`, `WXL_LAYOUT_SIZE(id, member, size, file, line)`; sorted by id, then offset and member, so two runs on the same input give byte-identical files and a run after a pull gives a reviewable diff |
| `layouts-compare.txt` | every hand constant of the inventory against its measurement: `OK`, `MISMATCH` (both values, the member it was mapped to, the hand comment), `SKIPPED` (with the reason from `layouts.txt`) or `FAILED` (the type did not compile) |
| `layouts-unmeasured.txt` | the types that failed to compile with the compiler's first error, the hand constants left without a measurement because of that, and the hand constants skipped on purpose with their reason |
| `layouts-summary.txt` | counts, the reconciliation, the flags read from the fork and the exact compile command, the environment the dumper reported, timing |

## How it compiles

1. `layouts.txt` becomes one translation unit, `build/layout_dumper.cpp`, that includes the
   fork's headers and prints one line per entry.
2. The fork's root `CMakeLists.txt` is read: the unconditional `add_compile_definitions`, the
   `CMAKE_SIZEOF_VOID_P EQUAL 4` branch's `add_compile_options` / `add_compile_definitions`, and
   `CMAKE_CXX_STANDARD`. The reader evaluates the conditions of that file for a Win32 MSVC build
   and refuses to run on a condition it does not know, so a flag added upstream is noticed.
3. The small CMake project in this folder is configured with Ninja inside a `vcvars32.bat`
   environment (found through `vswhere`), with those flags passed in as cache variables and the
   checkout root as the include directory, exactly as the fork's own targets get it. The dumper
   is built and run; it prints `sizeof(void*)`, `sizeof(time_t)`, `WOWDECOMP_ARCH_32`,
   `_M_IX86_FP` and `_MSC_VER` first, and the tool refuses a result that is not 32-bit.
4. If the translation unit does not compile, the tool bisects the type blocks with the compile
   command CMake generated (from `compile_commands.json`), records each failing type's first
   error in `layouts-unmeasured.txt`, and builds the rest.

The fork's own CMake cannot be reused directly: its Win32 configure stops at a `FATAL_ERROR`
when NASM is absent (SoundEngine assembles FMOD and FLAC sources), before any compile command
is generated. The standalone project reproduces the fork's flags instead, reading them from the
fork rather than copying them. The build type does not change the result: a Debug run gives a
byte-identical `layouts.def`.

## Running it

```
python core/tools/layouts/measure_layouts.py <path to the client checkout>
```

Python 3, standard library only; CMake and Visual Studio with the x86 MSVC toolchain on the
machine. The build goes to `core/tools/layouts/build/` (ignored, never committed); `--build`
moves it, `--out` writes the four files elsewhere, `--config` picks the CMake build type
(Release by default), `--quiet` silences the summary. A full run takes a few seconds.

## The invariant

Every row of the inventory is referenced exactly once in `layouts.txt`, either by a measured
entry or by a `skip` line with a reason, and every entry of `layouts.txt` ends up in
`layouts.def` or in `layouts-unmeasured.txt`. The tool refuses to write anything if the
references do not reconcile, and it fails if measured + skipped + failed is not the inventory
total. A table that silently omitted what it could not measure would be worse than no table.

## Reading a MISMATCH

A mismatch says the decompiled struct and the hand-written constant disagree. Either side can be
the wrong one: the fork's member may be misplaced, or the hand constant may describe something
other than the stock object (an over-allocated wrapper, a replacement table's own stride). The
comparison surfaces the pair; deciding needs the binary.

Where the hand comment and the fork's member name disagree about what a slot is, `layouts.txt`
maps the hand constant onto the fork member at that offset and says so in a comment. The
comparison is about offsets; the naming questions are left visible for a reader.

## When to re-run

After every pull of the fork, and after every edit of `layouts.txt`. Read `layouts-summary.txt`
and the diff of `layouts.def`; an entry that moves is either an upstream correction or an
upstream regression, and the compare file says which hand constant it touches.
