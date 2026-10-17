#!/usr/bin/env python3
# Self-test for check_seams.py: one synthetic tree per failure mode, and one that passes.
# Copyright (C) 2026 WarcraftXL
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
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
#error "define WXL_SEAM(domain, name, symbol, params) before including seams.def"
#endif
#include "wxl/seams/demo.def"
'''

DEMO_DEF = '''// The demo domain.
#ifndef WXL_SEAM
#error "define WXL_SEAM(domain, name, symbol, params) before including demo.def"
#endif
WXL_SEAM(Demo, Load, "Thing::Load", (const wxl_vec3* position, int32_t flags))
WXL_SEAM_RAISES(Demo, Load, Opened)
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
]

# (label, Demo.cpp body, demo.def replacement, exit code, text expected)
EVENT_CASES = [
    ("a correct body", BODY, DEMO_DEF, 0, None),
    ("the body stopped raising it", BODY.replace("WXL_EV_Opened", "WXL_EV_Closed"), DEMO_DEF,
     1, "the table says a Demo seam raises Opened, but Demo.cpp never raises it"),
    ("the body raises an undeclared event",
     BODY.replace("if (flags) host->Raise(WXL_EV_Opened, &args);",
                  "host->Raise(WXL_EV_Opened, &args); host->Raise(WXL_EV_Closed, &args);"),
     DEMO_DEF, 1, "raises Closed, which no WXL_SEAM_RAISES row of the Demo domain declares"),
    ("a row names an event the contract does not have", BODY,
     DEMO_DEF.replace("WXL_SEAM_RAISES(Demo, Load, Opened)",
                      "WXL_SEAM_RAISES(Demo, Load, Opened)\n"
                      "WXL_SEAM_RAISES(Demo, Load, Invented)"),
     1, "declares it raises Invented, which is not an event of events.def"),
    ("a raises row for a seam no row declares", BODY,
     DEMO_DEF + "WXL_SEAM_RAISES(Demo, Ghost, Closed)\n",
     1, "WXL_SEAM_RAISES names Demo.Ghost, which no WXL_SEAM row declares"),
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
