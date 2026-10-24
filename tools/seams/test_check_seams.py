#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 WarcraftXL
#
# Self-test for check_seams.py: one synthetic tree per failure mode, and one that passes.
"""A checker nobody has seen fail is not known to work. This makes it fail, on purpose, once per
rule it enforces, and pass on the tree that is correct."""

import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECKER = HERE / "check_seams.py"

SEAMS_DEF = '''// A synthetic seam table.
#ifndef WXL_SEAM
#error "define WXL_SEAM(domain, name, symbol, ret, conv, params) before including seams.def"
#endif
#include "wxl/seams/demo.def"
'''

DEMO_DEF = '''// The demo domain.
#ifndef WXL_SEAM
#error "define WXL_SEAM(domain, name, symbol, ret, conv, params) before including demo.def"
#endif
# Load and Count name their function plainly; Open writes the address too, as a row must when
# the client gives that name to more than one function.
WXL_SEAM(Demo, Load, "Thing::Load", void, cdecl, (const wxl_vec3* position, int32_t flags))
WXL_SEAM_RAISES(Demo, Load, Opened)
WXL_SEAM(Demo, Count, "Thing::Count", int32_t, thiscall, (void* self, int32_t flags))
WXL_SEAM_RAISES(Demo, Count, Closed)
WXL_SEAM_GATE(Demo, Open, "Thing::Open@0x00401000", int32_t, cdecl, (const char* path, uint32_t mode))
WXL_SEAM_RAISES(Demo, Open, Opened)
WXL_SEAM_AFTER(Demo, Close, "Thing::Close", void, cdecl, (int32_t flags))
'''

CONTRACT = '''// A synthetic event table.
WXL_EVENT(Demo, Opened, 0, wxl_demo_args)
WXL_EVENT(Demo, Closed, 1, wxl_demo_args)
'''

BODY = '''#include "wxl/seams/seams.h"

namespace wxl::seam::Demo::Load {
    void Before(const wxl_vec3* position, int32_t flags) {
        if (flags) host->Raise(WXL_EV_Opened, &args);
    }
    void After(const wxl_vec3* position, int32_t flags) {}
}

namespace wxl::seam::Demo::Count {
    void Before(void* self, int32_t flags) {}
    void After(int32_t result, void* self, int32_t flags) {
        if (result) host->Raise(WXL_EV_Closed, &args);
    }
}
'''

GOOD = '''#include "WarcraftXL/Seams.h"

namespace {
    int helper(int x) { return x + 1; }
}

void Thing::Load(const C3Vector& position, int32_t flags) {
    wxl::seam::Demo::Load::Before(WarcraftXL::Vec3(position), flags);
    if (!flags) {
        wxl::seam::Demo::Load::After(WarcraftXL::Vec3(position), flags);
        return;
    }
    helper(flags);
    wxl::seam::Demo::Load::After(WarcraftXL::Vec3(position), flags);
}

int32_t Thing::Count(int32_t flags) {
    wxl::seam::Demo::Count::Before(this, flags);
    int32_t count = helper(flags);
    wxl::seam::Demo::Count::After(count, this, flags);
    return count;
}

int32_t Thing::Open(const char* path, uint32_t mode) {
    int32_t answer = 0;
    if (wxl::seam::Demo::Open::Gate(&answer, &path, &mode)) {
        return answer;
    }
    return helper(mode);
}

void Thing::Close(int32_t flags) {
    if (!flags) {
        wxl::seam::Demo::Close::After(flags);
        return;
    }
    helper(flags + 1);
    wxl::seam::Demo::Close::After(flags);
}
'''

# (label, Thing.cpp, Demo.cpp body, demo.def extra rows, exit code, text expected)
CASES = [
    ("a correct tree", GOOD, 0, None),
    ("Before deleted",
     GOOD.replace("    wxl::seam::Demo::Load::Before(WarcraftXL::Vec3(position), flags);\n", ""),
     1, "the fork never calls wxl::seam::Demo::Load::Before"),
    ("every After deleted",
     GOOD.replace("wxl::seam::Demo::Load::After(WarcraftXL::Vec3(position), flags);", "return;"),
     1, "the fork never calls wxl::seam::Demo::Load::After"),
    ("a call moved to another function",
     GOOD.replace("    int helper(int x) { return x + 1; }",
                  "    int helper(int x) { wxl::seam::Demo::Load::Before(0, x); return x; }")
         .replace("    wxl::seam::Demo::Load::Before(WarcraftXL::Vec3(position), flags);\n", ""),
     1, "but the row names Thing::Load"),
    ("an argument dropped",
     GOOD.replace("Before(WarcraftXL::Vec3(position), flags)", "Before(WarcraftXL::Vec3(position))"),
     1, "is passed 1 argument(s); the row declares 2"),
    ("the arguments swapped",
     GOOD.replace("Before(WarcraftXL::Vec3(position), flags)",
                  "Before(flags, WarcraftXL::Vec3(position))"),
     1, "does not carry the row's parameter `position`"),
    ("a call to a seam no row declares",
     GOOD + "\nvoid Thing::Other() { wxl::seam::Demo::Ghost::Before(); }\n",
     1, "no row of the seam table declares Demo.Ghost"),
    ("two calls to Before",
     GOOD.replace("    helper(flags);",
                  "    wxl::seam::Demo::Load::Before(WarcraftXL::Vec3(position), flags);"),
     1, "2 calls to wxl::seam::Demo::Load::Before"),
    ("the result dropped from a returning seam's After",
     GOOD.replace("Count::After(count, this, flags)", "Count::After(this, flags)"),
     1, "is passed 2 argument(s); the row declares 3 (the int32_t result, then self, flags)"),
    ("`this` dropped from a thiscall seam's Before",
     GOOD.replace("Count::Before(this, flags)", "Count::Before(flags)"),
     1, "is passed 1 argument(s); the row declares 2"),
    ("Gate deleted from a gated seam",
     GOOD.replace("    if (wxl::seam::Demo::Open::Gate(&answer, &path, &mode)) {\n"
                  "        return answer;\n    }\n", ""),
     1, "the fork never calls wxl::seam::Demo::Open::Gate"),
    ("two calls to Gate",
     GOOD.replace("    return helper(mode);",
                  "    wxl::seam::Demo::Open::Gate(&answer, &path, &mode);\n    return helper(mode);"),
     1, "2 calls to wxl::seam::Demo::Open::Gate"),
    ("an After called on a gated seam",
     GOOD.replace("    return helper(mode);",
                  "    answer = helper(mode);\n"
                  "    wxl::seam::Demo::Open::After(answer, &path, &mode);\n    return answer;"),
     1, "Demo.Open is a WXL_SEAM_GATE row, which declares Gate only"),
    ("a Gate called on an observing seam",
     GOOD.replace("wxl::seam::Demo::Count::Before(this, flags);",
                  "wxl::seam::Demo::Count::Gate(this, flags);"),
     1, "Demo.Count is a WXL_SEAM row, which declares Before and After only"),
    ("a Before called on an After-only seam",
     GOOD.replace("void Thing::Close(int32_t flags) {\n",
                  "void Thing::Close(int32_t flags) {\n    wxl::seam::Demo::Close::Before(flags);\n"),
     1, "Demo.Close is a WXL_SEAM_AFTER row, which declares After only"),
    ("the only side of a one-sided seam deleted",
     GOOD.replace("wxl::seam::Demo::Close::After(flags);", "(void)flags;"),
     1, "the fork never calls wxl::seam::Demo::Close::After"),
    ("the result pointer dropped from a gated seam's Gate",
     GOOD.replace("Open::Gate(&answer, &path, &mode)", "Open::Gate(&path, &mode)"),
     1, "is passed 2 argument(s); the row declares 3 "
        "(a pointer to the int32_t to answer with, then path, mode)"),
]

# (label, Demo.cpp body, demo.def replacement, exit code, text expected)
EVENT_CASES = [
    ("a correct body", BODY, DEMO_DEF, 0, None),
    ("the body stopped raising it", BODY.replace("WXL_EV_Opened", "WXL_EV_Closed"), DEMO_DEF,
     1, "the table says a Demo seam raises Opened, but Demo.cpp never raises it"),
    ("the body raises an undeclared event",
     BODY.replace("if (flags) host->Raise(WXL_EV_Opened, &args);",
                  "host->Raise(WXL_EV_Opened, &args); host->Raise(WXL_EV_Broken, &args);"),
     DEMO_DEF, 1, "raises Broken, which no WXL_SEAM_RAISES row of the Demo domain declares"),
    ("a row names an event the contract does not have", BODY,
     DEMO_DEF.replace("WXL_SEAM_RAISES(Demo, Load, Opened)",
                      "WXL_SEAM_RAISES(Demo, Load, Opened)\n"
                      "WXL_SEAM_RAISES(Demo, Load, Invented)"),
     1, "declares it raises Invented, which is not an event of events.def"),
    ("a raises row for a seam no row declares", BODY,
     DEMO_DEF + "WXL_SEAM_RAISES(Demo, Ghost, Closed)\n",
     1, "WXL_SEAM_RAISES names Demo.Ghost, which no WXL_SEAM row declares"),
    ("a row without its ABI columns", BODY,
     DEMO_DEF.replace('"Thing::Load", void, cdecl, ', '"Thing::Load", '),
     1, "a WXL_SEAM row has 4 columns, expected 6"),
    ("a row with a convention the generator does not know", BODY,
     DEMO_DEF.replace("void, cdecl,", "void, pascal,"),
     1, "the conv column of Demo.Load is `pascal`"),
    ("a thiscall row whose first parameter is not a pointer", BODY,
     DEMO_DEF.replace("thiscall, (void* self, int32_t flags)", "thiscall, (int32_t self, int32_t flags)"),
     1, "is thiscall but its first parameter is not `this` as a pointer"),
]


def build(case, source=GOOD, body=BODY, demo_def=DEMO_DEF):
    for sub in ("seams", "bodies", "contract", "client/Src"):
        (case / sub).mkdir(parents=True, exist_ok=True)
    (case / "seams" / "seams.def").write_text(SEAMS_DEF, encoding="utf-8")
    (case / "seams" / "demo.def").write_text(demo_def, encoding="utf-8")
    (case / "bodies" / "Demo.cpp").write_text(body, encoding="utf-8")
    (case / "contract" / "events.def").write_text(CONTRACT, encoding="utf-8")
    (case / "client" / "Src" / "Thing.cpp").write_text(source, encoding="utf-8")


def run(case):
    return subprocess.run(
        [sys.executable, "-I", str(CHECKER),
         "--seams", str(case / "seams"), "--client", str(case / "client"),
         "--bodies", str(case / "bodies"),
         "--contract", str(case / "contract" / "events.def")],
        capture_output=True, text=True)


def main():
    failures = 0
    with tempfile.TemporaryDirectory() as temp:
        every = ([(label, src, BODY, DEMO_DEF, code, text)
                  for label, src, code, text in CASES]
                 + [(label, GOOD, body, demo_def, code, text)
                    for label, body, demo_def, code, text in EVENT_CASES])
        for index, (label, source, body, demo_def, want_code, want_text) in enumerate(every):
            case = Path(temp) / ("case%02d" % index)
            build(case, source, body, demo_def)
            result = run(case)
            output = result.stdout + result.stderr
            ok = result.returncode == want_code
            if want_text and want_text not in output:
                ok = False
            print("%-4s %-36s exit %d" % ("ok" if ok else "FAIL", label, result.returncode))
            if not ok:
                failures += 1
                print("     expected exit %d%s" % (want_code,
                      ", with %r" % want_text if want_text else ""))
                for line in output.strip().splitlines():
                    print("     | " + line)

    print("\n%d case(s), %d failure(s)" % (len(CASES) + len(EVENT_CASES), failures))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
