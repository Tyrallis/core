# seams: the wiring check

`check_seams.py` compares the seam table in `core/include/wxl/seams/*.def` with the client fork's
sources and fails when the two disagree.

## Why it exists

A seam is declared once and realised twice.

The **x86** realisation is generated: `core/src/seams/AttachX86.cpp` expands every row of the table
into a detour and attaches it at `WXL_SITE(symbol, 0)`. Nothing can drift, because nothing is
written by hand -- a row that loses its symbol fails the build in `WXL_SITE` at compile time.

The **x64** realisation is a pair of calls sitting inside an upstream function, in a fork that keeps
pulling from upstream. A merge that drops one of those lines is silent: the build succeeds, the
runtime starts, the seam logs that it is live, and the event simply never fires again. No error, no
log, nothing. That is the one failure mode this design has, and this tool is what turns it into a
build failure.

## What it checks

For every row of the seam table:

| | |
| --- | --- |
| **present** | the fork calls `wxl::seam::<Domain>::<Name>::Before`, and `::After`, at least once each |
| **once in** | exactly one `Before`: a function is entered once. `After` may repeat, one per return path |
| **in place** | every call sits inside the function the row's `symbol` column names, not another one |
| **arity** | the call passes as many arguments as the row declares parameters |
| **arguments** | argument *n* carries the row's parameter *n* by name |

and, in the other direction:

| | |
| --- | --- |
| **declared** | a `wxl::seam::` call in the fork whose `<Domain>.<Name>` no row declares is an error |

The argument rule compares names, not types: argument *n*'s expression must contain the row's *n*th
parameter name, ignoring case and underscores. `WarcraftXL::Vec3(position)` carries `position`;
`entity->m_guid` carries `guid`. It exists to catch a swapped pair, which arity alone would not --
the call would still compile if the two happened to be convertible. It is deliberately a wiring
check and not a type check: the compiler does types, and does them better. Its known limit is that
an unrelated expression which happens to spell the parameter's name passes, so it narrows the hole
rather than closing it.

The tool reads C++ with a brace-and-scope scanner, not a compiler: it blanks comments, literals and
preprocessor directives, then tracks namespace, class and function scopes to say which function a
call is in. It reports a file it could not read as a problem rather than passing it.

## Running it

```sh
python core/tools/seams/check_seams.py            # from the core checkout's parent
python core/tools/seams/check_seams.py -v         # list every seam and where it is wired
python core/tools/seams/check_seams.py --client <path to the fork> --seams <path to the tables>
```

Python 3, standard library only. It exits 0 when everything is wired, 1 with one line per problem
otherwise, and 2 when it was pointed at something that is not a checkout. By default it finds both
inputs from its own location: the seam tables in the core checkout above it, the fork in `client`
beside that checkout.

## In the build

`client/WarcraftXL/CMakeLists.txt` declares a `wxl-seam-check` target that runs the tool, and
`WarcraftXLClient` depends on it, so a 64-bit build of the client runs the check and fails on a
dropped call site. It is declared there and not in the fork's root `CMakeLists.txt` on purpose:
`core/tools/layouts/measure_layouts.py` parses that root file and refuses a condition it does not
recognise, so nothing conditional may be added to it.

If Python is not on the machine, the target is not declared and the build says so once. The check
is then the manual command above.

## What it does not prove

That a seam's body raises the right event with the right payload. That is a different question and
`core/tests/seam_harness.cpp` answers it: it links the seam bodies themselves against a real
`ModHost` whose raisable events are the ones the table derives, subscribes to each, calls every
seam's `Before` and `After`, and checks what arrived. Built for both architectures as
`wxl-core-seam-harness`, and registered with ctest as `seams`.

Between them: `WXL_SITE` proves the x86 address resolves, this tool proves the x64 calls are
where the table says, and the harness proves the body in the middle does what the row promises.
None of the three can prove a detour behaves correctly in a running client.

## Self-test

`test_check_seams.py` builds small synthetic trees in a temporary directory -- a correct one, and
one per failure mode -- and asserts the tool's exit code and the problem it names for each. It
needs nothing but Python:

```sh
python core/tools/seams/test_check_seams.py
```
