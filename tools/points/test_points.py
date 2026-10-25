#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 WarcraftXL
#
# Checks generate_points.py and insert_points.py against a small fork made up on the spot.
"""Self-test of the point tools: a fork of three functions, a functions.def, a points.txt."""

import shutil
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import generate_points  # noqa: E402
import insert_points  # noqa: E402
import points_table  # noqa: E402

SOURCE = """#include "Thing.h"

// OFFSET: 0x00401000
int32_t Thing::Add(int32_t a, const char* name) {
    return a;
}

// OFFSET: 0x00401100
void Free(uint32_t flags) {
    Thing::Helper(flags);
}

// INLINED
int32_t Thing::Inlined() {
    return 1;
}

// OFFSET: 0x00401200
void Thing::Helper(uint32_t flags) {
}
"""

HEADER = """class Thing {
public:
    int32_t Add(int32_t a, const char* name);
    static void Helper(uint32_t flags);
    int32_t Inlined();
};
"""

FUNCTIONS = """WXL_OFFSET_FUNCTION("Thing::Add", 0x00401000, 0x100, 0x40, "manual", "", "Src/Thing.cpp", 4)
WXL_OFFSET_FUNCTION("Free", 0x00401100, 0x100, 0x20, "manual", "", "Src/Thing.cpp", 9)
WXL_OFFSET_FUNCTION("Thing::Helper", 0x00401200, 0x10, 0x3, "manual", "", "Src/Thing.cpp", 19)
WXL_OFFSET_FUNCTION("Other::Folded", 0x00401200, 0x10, 0x3, "manual", "", "Src/Thing.cpp", 19)
"""

LIST = """0x00401000  Thing::Add     replace  main  stable       # a member
0x00401100  Free           observe  any   provisional  # a free function
-           Thing::Inlined replace  main  provisional  # x64 only
"""


class Report:
    def __init__(self):
        self.total = self.failures = 0

    def __call__(self, what, ok, detail=""):
        self.total += 1
        if not ok:
            self.failures += 1
        print("%-4s %s%s" % ("ok" if ok else "FAIL", what, "" if ok else "\n     " + detail))


def make(root):
    fork = root / "fork"
    (fork / "Src").mkdir(parents=True)
    (fork / "WarcraftXL").mkdir()
    (fork / "Src" / "Thing.cpp").write_text(SOURCE)
    (fork / "Src" / "Thing.h").write_text(HEADER)
    offsets = root / "offsets"
    offsets.mkdir()
    (offsets / "functions.def").write_text(FUNCTIONS)
    (root / "points.txt").write_text(LIST)
    (root / "sdk" / "include" / "wxl").mkdir(parents=True)
    return fork, offsets


def main():
    report = Report()
    root = Path(tempfile.mkdtemp(prefix="wxl-points-"))
    try:
        fork, offsets = make(root)
        points, problems = points_table.load(root / "points.txt", fork, offsets)
        report("the list reads without a problem", not problems, "; ".join(problems))
        add, free, inlined = points
        report("a member takes `self` first and is thiscall", add.member and add.convention == "thiscall"
               and add.lowered == "i32(ptr,i32,ptr)", add.lowered)
        report("a free function is cdecl, its pointer lowered", not free.member and free.convention == "cdecl"
               and free.lowered == "void(u32)", free.lowered)
        report("an // INLINED function is x64 only", inlined.x64 and not inlined.x86 and inlined.member, inlined.x86_why)
        report("the layout hash is the lowered signature's", add.layout_hash == points_table.fnv1a32("i32(ptr,i32,ptr)"))

        folded = root / "folded.txt"
        folded.write_text("0x00401200  Thing::Helper  replace  main  stable\n")
        points2, _ = points_table.load(folded, fork, offsets)
        report("a folded address is no x86 point, and says why", not points2[0].x86 and "folded" in points2[0].x86_why,
               points2[0].x86_why)

        bad = root / "bad.txt"
        bad.write_text("0x00401000  Thing::Wrong  replace  main  stable\n0x00401100  Free  sometimes  main  stable\n")
        _, problems = points_table.load(bad, fork, offsets)
        report("a name the tag does not give and a bad policy are problems", len(problems) == 2, "; ".join(problems))

        argv = ["--list", str(root / "points.txt"), "--client", str(fork), "--sdk", str(root / "sdk"),
                "--offsets", str(offsets), "--x86", str(root / "x86.def"), "--quiet"]
        report("generate writes the tables", generate_points.main(argv) == 0)
        report("and --check finds them current", generate_points.main(argv + ["--check"]) == 0)
        contract = (root / "sdk" / "include" / "wxl" / "points.def").read_text()
        report("the contract row of a member", 'WXL_POINT(Thing_Add, "Thing::Add", int32_t, (void* self, int32_t a, '
               'void* name), "i32(ptr,i32,ptr)"' in contract, contract[-600:])
        (root / "points.txt").write_text(LIST.replace("observe", "replace"))
        report("a changed list makes --check fail", generate_points.main(argv + ["--check"]) == 1)
        (root / "points.txt").write_text(LIST)
        generate_points.main(argv)

        iargs = ["--list", str(root / "points.txt"), "--client", str(fork), "--offsets", str(offsets)]
        report("--check fails on a fork without its lines", insert_points.main(iargs + ["--check"]) == 1)
        report("--write writes them", insert_points.main(iargs + ["--write"]) == 0)
        text = (fork / "Src" / "Thing.cpp").read_text()
        report("a member's line passes `this`, first in its body",
               "int32_t Thing::Add(int32_t a, const char* name) {\n    WXL_POINT_0(this, a, name); // wxl.point" in text, text)
        report("and the include is added", '#include "WarcraftXL/Points.h"' in text)
        report("--check passes on what --write wrote", insert_points.main(iargs + ["--check"]) == 0)
        (fork / "Src" / "Thing.cpp").write_text(text.replace("WXL_POINT_0(this, a, name);", "WXL_POINT_0(this, a);"))
        report("a line whose arguments are not the table's fails --check", insert_points.main(iargs + ["--check"]) == 1)
        (fork / "Src" / "Thing.cpp").write_text(text.replace("    return a;", "    WXL_POINT_7(a); // wxl.point\n    return a;"))
        report("a line of no point fails --check", insert_points.main(iargs + ["--check"]) == 1)
        report("--write repairs both", insert_points.main(iargs + ["--write"]) == 0 and
               "WXL_POINT_7" not in (fork / "Src" / "Thing.cpp").read_text())
    finally:
        shutil.rmtree(root, ignore_errors=True)
    print("\n%d case(s), %d failure(s)" % (report.total, report.failures))
    return 1 if report.failures else 0


if __name__ == "__main__":
    sys.exit(main())
