#!/usr/bin/env python3
# Checks that every seam declared in core/include/wxl/seams/*.def is wired into the client fork.
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
"""Verify the x64 realisation of the seam table against the client fork's sources.

The x86 realisation is generated from the table itself, so it cannot drift. The x64 one is a
pair of calls living inside an upstream function, which a merge can silently drop. This tool is
what makes that loud. See README.md next to it.
"""

import argparse
import re
import sys
from pathlib import Path

# --- The seam table -------------------------------------------------------------------------

SEAM_ROW = re.compile(r"\bWXL_SEAM\s*\(", re.MULTILINE)
RAISES_ROW = re.compile(r"\bWXL_SEAM_RAISES\s*\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\)")
EVENT_ROW = re.compile(r"\bWXL_EVENT\s*\(\s*\w+\s*,\s*(\w+)\s*,")
RAISE_CALL = re.compile(r"\bRaise\s*\(\s*WXL_EV_(\w+)")
INCLUDE_DEF = re.compile(r'^\s*#\s*include\s+"wxl/seams/([A-Za-z0-9_]+\.def)"', re.MULTILINE)


class Seam:
    """One WXL_SEAM row: where it was declared, and what it promises the fork calls."""

    def __init__(self, domain, name, symbol, params, origin):
        self.domain = domain
        self.name = name
        self.symbol = symbol
        self.params = params  # list of parameter names, in order
        self.origin = origin  # "<file>:<line>" of the row
        self.raises = []      # event names, without the WXL_EV_ prefix

    @property
    def point(self):
        return "%s.%s" % (self.domain, self.name)

    @property
    def qualified(self):
        return "wxl::seam::%s::%s" % (self.domain, self.name)


def split_top_level(text, sep=","):
    """Split on `sep` at nesting level zero of (), [], <> and {}."""
    out, depth, angle, start = [], 0, 0, 0
    for i, ch in enumerate(text):
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "<":
            angle += 1
        elif ch == ">" and angle:
            angle -= 1
        elif ch == sep and depth == 0 and angle == 0:
            out.append(text[start:i])
            start = i + 1
    out.append(text[start:])
    return [part.strip() for part in out]


def balanced(text, open_at):
    """Index just past the ')' matching the '(' at `open_at`, or -1."""
    depth = 0
    for i in range(open_at, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


def parameter_name(decl):
    """The parameter's name out of its declaration: the last identifier that is not a keyword."""
    decl = re.sub(r"\[.*?\]", " ", decl)
    identifiers = re.findall(r"[A-Za-z_]\w*", decl)
    for ident in reversed(identifiers):
        if ident not in ("const", "volatile", "struct", "unsigned", "signed", "void"):
            return ident
    return ""


def read_table(seams_dir, problems):
    """Every row of seams.def, in the order its domain files are included."""
    root = seams_dir / "seams.def"
    if not root.is_file():
        problems.append("the seam table is missing: %s" % root)
        return []

    files = [root]
    root_text = strip_cxx(root.read_text(encoding="utf-8", errors="replace"),
                           keep_strings=True, keep_directives=True)
    for match in INCLUDE_DEF.finditer(root_text):
        files.append(seams_dir / match.group(1))

    seams = []
    for path in files:
        if not path.is_file():
            problems.append("seams.def includes %s, which does not exist" % path.name)
            continue
        text = strip_cxx(path.read_text(encoding="utf-8", errors="replace"), keep_strings=True)
        for match in SEAM_ROW.finditer(text):
            open_at = match.end() - 1
            close = balanced(text, open_at)
            if close < 0:
                problems.append("%s: a WXL_SEAM row has no closing parenthesis" % path.name)
                continue
            columns = split_top_level(text[open_at + 1:close - 1])
            line = text.count("\n", 0, match.start()) + 1
            origin = "%s:%d" % (path.name, line)
            if len(columns) != 4:
                problems.append("%s: a WXL_SEAM row has %d columns, expected 4 "
                                "(domain, name, symbol, params)" % (origin, len(columns)))
                continue
            domain, name, symbol, params = columns
            symbol = symbol.strip().strip('"')
            if not (params.startswith("(") and params.endswith(")")):
                problems.append("%s: the params column of %s.%s is not parenthesised"
                                % (origin, domain, name))
                continue
            inner = params[1:-1].strip()
            names = [] if not inner else [parameter_name(p) for p in split_top_level(inner)]
            seams.append(Seam(domain, name, symbol, names, origin))

        by_point = {s.point: s for s in seams}
        for match in RAISES_ROW.finditer(text):
            point = "%s.%s" % (match.group(1), match.group(2))
            line = text.count("\n", 0, match.start()) + 1
            seam = by_point.get(point)
            if seam is None:
                problems.append("%s:%d: WXL_SEAM_RAISES names %s, which no WXL_SEAM row declares"
                                % (path.name, line, point))
                continue
            if match.group(3) in seam.raises:
                problems.append("%s:%d: %s declares it raises %s twice"
                                % (path.name, line, point, match.group(3)))
                continue
            seam.raises.append(match.group(3))
    return seams


# --- Reading C++ ----------------------------------------------------------------------------

def strip_cxx(text, keep_strings=False, keep_directives=False):
    """Blank out comments, string and character literals, and preprocessor directives.

    Line count and every other character's offset are preserved, so a position in the result is a
    position in the original. `keep_strings` and `keep_directives` leave literals and
    directives alone, for the seam tables, whose rows live among both.
    """
    out = list(text)
    i, n = 0, len(text)
    at_line_start = True
    while i < n:
        ch = text[i]
        if ch == "\n":
            at_line_start = True
            i += 1
            continue
        if ch in " \t\r":
            i += 1
            continue
        if at_line_start and ch == "#" and not keep_directives:
            # The whole directive, including backslash-continued lines.
            while i < n:
                if text[i] == "\n" and not (i and text[i - 1] == "\\"):
                    break
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            continue
        at_line_start = False
        if ch == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out[i] = " "
                i += 1
            continue
        if ch == "/" and i + 1 < n and text[i + 1] == "*":
            out[i] = out[i + 1] = " "
            i += 2
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                if text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = out[i + 1] = " "
                i += 2
            continue
        if ch in "\"'":
            quote = ch
            if keep_strings:
                i += 1
                while i < n and text[i] != quote:
                    i += 2 if text[i] == "\\" else 1
                i += 1
                continue
            out[i] = " "
            i += 1
            while i < n and text[i] != quote:
                if text[i] == "\\":
                    out[i] = " "
                    i += 1
                if i < n and text[i] != "\n":
                    out[i] = " "
                i += 1
            if i < n:
                out[i] = " "
                i += 1
            continue
        i += 1
    return "".join(out)


CONTROL = {"if", "for", "while", "switch", "catch", "else", "do", "return", "throw",
           "sizeof", "decltype", "alignof", "static_assert", "noexcept"}

NAMESPACE = re.compile(r"\bnamespace\b\s*([\w:]*)\s*$")
RECORD = re.compile(r"\b(class|struct|union|enum)\b[\w\s:]*?([A-Za-z_]\w*)\s*(?::[^{]*)?$")


class Scope:
    __slots__ = ("kind", "name")

    def __init__(self, kind, name):
        self.kind = kind
        self.name = name


def classify(pending):
    """What a '{' opened, judged from the declarator text before it."""
    pending = pending.strip()
    if not pending:
        return Scope("block", "")

    match = NAMESPACE.search(pending)
    if match:
        return Scope("namespace", match.group(1))

    # A function definition is a declarator with a parameter list: an identifier, then a '(' at
    # the top level, and nothing after the matching ')' that makes it an initialiser.
    open_at = -1
    depth = 0
    for i, ch in enumerate(pending):
        if ch == "(":
            if depth == 0:
                open_at = i
                break
            depth += 1
        elif ch == ")":
            depth -= 1
    if open_at > 0:
        before = pending[:open_at].rstrip()
        ident = re.search(r"([A-Za-z_~][\w]*(?:\s*::\s*~?[\w]+)*)\s*$", before)
        if ident:
            tail = ident.group(1).split("::")[-1].strip()
            if tail not in CONTROL and not before.rstrip().endswith("="):
                name = re.sub(r"\s*", "", ident.group(1))
                return Scope("function", name)

    match = RECORD.search(pending)
    if match:
        return Scope("record", match.group(2))

    return Scope("block", "")


def enclosing_functions(text):
    """For each '{'-delimited scope, map a character offset to the qualified function it is in.

    Returns a sorted list of (start, end, qualified_name) for every function body found, innermost
    last, so a lookup takes the last range that contains the offset.
    """
    stack = []
    bodies = []
    pending_start = 0
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "{":
            scope = classify(text[pending_start:i])
            prefix = [s.name for s in stack if s.kind in ("namespace", "record") and s.name]
            if scope.kind == "function":
                qualified = "::".join(prefix + [scope.name]) if prefix else scope.name
                # A name written qualified already carries its own prefix.
                if "::" in scope.name and prefix:
                    head = scope.name.split("::")[0]
                    if head in prefix:
                        qualified = "::".join(prefix[:prefix.index(head)] + [scope.name])
                stack.append(Scope("function", scope.name))
                bodies.append([i, None, qualified])
            else:
                stack.append(scope)
            pending_start = i + 1
        elif ch == "}":
            if not stack:
                return bodies, "unbalanced '}'"
            scope = stack.pop()
            if scope.kind == "function":
                for body in reversed(bodies):
                    if body[1] is None:
                        body[1] = i
                        break
            pending_start = i + 1
        elif ch == ";":
            pending_start = i + 1
        i += 1
    return bodies, None


def function_at(bodies, offset):
    found = None
    for start, end, name in bodies:
        if end is None:
            end = 1 << 62
        if start < offset < end:
            found = name
    return found


# --- The fork's call sites ------------------------------------------------------------------

CALL = re.compile(r"\bwxl\s*::\s*seam\s*::\s*(\w+)\s*::\s*(\w+)\s*::\s*(Before|After)\s*\(")


class Call:
    def __init__(self, domain, name, half, args, where, enclosing):
        self.domain = domain
        self.name = name
        self.half = half
        self.args = args
        self.where = where
        self.enclosing = enclosing

    @property
    def point(self):
        return "%s.%s" % (self.domain, self.name)


def normalise(text):
    return re.sub(r"[^a-z0-9]", "", text.lower())


def scan_sources(client_dir, excluded, problems):
    calls = []
    skip_dirs = {"build", "bin", "ThirdParty", ".git", "out"}
    for path in sorted(client_dir.rglob("*")):
        if path.suffix.lower() not in (".cpp", ".h", ".hpp", ".cc", ".cxx"):
            continue
        if any(part in skip_dirs for part in path.relative_to(client_dir).parts[:-1]):
            continue
        if path.resolve() in excluded:
            continue
        raw = path.read_text(encoding="utf-8", errors="replace")
        if "wxl::seam" not in raw and "wxl ::" not in raw:
            continue
        text = strip_cxx(raw)
        if not CALL.search(text):
            continue
        bodies, trouble = enclosing_functions(text)
        rel = path.relative_to(client_dir).as_posix()
        if trouble:
            problems.append("%s: could not be read as C++ (%s); a seam call in it cannot be "
                            "checked" % (rel, trouble))
            continue
        for match in CALL.finditer(text):
            close = balanced(text, match.end() - 1)
            if close < 0:
                problems.append("%s: a seam call has no closing parenthesis" % rel)
                continue
            inner = text[match.end():close - 1].strip()
            args = [] if not inner else split_top_level(inner)
            line = text.count("\n", 0, match.start()) + 1
            calls.append(Call(match.group(1), match.group(2), match.group(3), args,
                              "%s:%d" % (rel, line), function_at(bodies, match.start())))
    return calls


# --- The check ------------------------------------------------------------------------------

def check_events(seams, bodies_dir, contract, problems):
    """The WXL_SEAM_RAISES rows against the contract and against the bodies that raise them."""
    known = None
    if contract and contract.is_file():
        text = strip_cxx(contract.read_text(encoding="utf-8", errors="replace"))
        known = {m.group(1) for m in EVENT_ROW.finditer(text)}
        if not known:
            known = None
    if known is None:
        problems.append("the contract's event table was not readable at %s; the events the seam "
                        "table names cannot be checked against it" % contract)

    for seam in seams:
        for event in seam.raises:
            if known is not None and event not in known:
                problems.append("%s (%s): declares it raises %s, which is not an event of "
                                "events.def" % (seam.point, seam.origin, event))

    # Each domain's body is the one place its events are raised, on both architectures.
    for domain in sorted({seam.domain for seam in seams}):
        rows = [seam for seam in seams if seam.domain == domain]
        declared = {event for seam in rows for event in seam.raises}
        body = bodies_dir / ("%s.cpp" % domain)
        if not body.is_file():
            problems.append("the %s domain has rows but no body at %s" % (domain, body))
            continue
        text = strip_cxx(body.read_text(encoding="utf-8", errors="replace"))
        raised = {m.group(1) for m in RAISE_CALL.finditer(text)}
        for event in sorted(declared - raised):
            problems.append("%s: the table says a %s seam raises %s, but %s.cpp never raises it"
                            % (body.name, domain, event, domain))
        for event in sorted(raised - declared):
            problems.append("%s: raises %s, which no WXL_SEAM_RAISES row of the %s domain declares;"
                            " the event would not be published to mods" % (body.name, event, domain))


def check(seams, calls, problems):
    declared = {seam.point: seam for seam in seams}

    for call in calls:
        if call.point not in declared:
            problems.append("%s: calls %s::%s, but no row of the seam table declares %s"
                            % (call.where, call.point.replace(".", "::"), call.half, call.point))

    for seam in seams:
        mine = [c for c in calls if c.point == seam.point]
        for half in ("Before", "After"):
            halves = [c for c in mine if c.half == half]
            if not halves:
                problems.append("%s (%s): the fork never calls %s::%s; the seam is declared and "
                                "dead" % (seam.point, seam.origin, seam.qualified, half))
                continue
            if half == "Before" and len(halves) > 1:
                problems.append("%s: %d calls to %s::Before (%s); a seam enters its function once"
                                % (seam.point, len(halves), seam.qualified,
                                   ", ".join(c.where for c in halves)))
            for call in halves:
                if call.enclosing != seam.symbol:
                    problems.append("%s: %s::%s is called from %s, but the row names %s"
                                    % (call.where, seam.qualified, half,
                                       call.enclosing or "no function the tool could identify",
                                       seam.symbol))
                if len(call.args) != len(seam.params):
                    problems.append("%s: %s::%s is passed %d argument(s); the row declares %d (%s)"
                                    % (call.where, seam.qualified, half, len(call.args),
                                       len(seam.params), ", ".join(seam.params) or "none"))
                    continue
                for index, (arg, param) in enumerate(zip(call.args, seam.params)):
                    if param and normalise(param) not in normalise(arg):
                        problems.append("%s: %s::%s argument %d is `%s`, which does not carry the "
                                        "row's parameter `%s`"
                                        % (call.where, seam.qualified, half, index + 1,
                                           " ".join(arg.split()), param))


def main():
    here = Path(__file__).resolve().parent
    core = here.parent.parent
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--seams", type=Path, default=core / "include" / "wxl" / "seams",
                        help="the directory holding seams.def and the domain tables")
    parser.add_argument("--client", type=Path, default=core.parent / "client",
                        help="the client fork's checkout")
    parser.add_argument("--bodies", type=Path, default=core / "src" / "seams",
                        help="the directory holding one <Domain>.cpp body per domain")
    parser.add_argument("--contract", type=Path,
                        default=core.parent / "sdk" / "include" / "wxl" / "events.def",
                        help="the SDK's event table, read to check the events a seam names")
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="list every seam and where it is wired")
    args = parser.parse_args()

    problems = []
    seams_dir = args.seams.resolve()
    client_dir = args.client.resolve()
    if not client_dir.is_dir():
        print("check_seams: --client (%s) is not a directory" % client_dir, file=sys.stderr)
        return 2

    seams = read_table(seams_dir, problems)
    # Seams.h declares the table's functions; it is not a call site.
    excluded = {(client_dir / "WarcraftXL" / "Seams.h").resolve()}
    calls = scan_sources(client_dir, excluded, problems)
    check_events(seams, args.bodies.resolve(), args.contract.resolve(), problems)
    check(seams, calls, problems)

    if args.verbose:
        for seam in seams:
            mine = [c for c in calls if c.point == seam.point]
            print("%-28s %-40s %-22s %s"
                  % (seam.point, seam.symbol, ",".join(seam.raises) or "-",
                     ", ".join("%s %s" % (c.half, c.where) for c in mine) or "NOT WIRED"))

    if problems:
        print("check_seams: %d problem(s) between the seam table and %s"
              % (len(problems), client_dir.name), file=sys.stderr)
        for problem in problems:
            print("  " + problem, file=sys.stderr)
        return 1

    print("check_seams: %d seam(s), %d event(s), %d call site(s), all wired as the table declares"
          % (len(seams), len({e for s in seams for e in s.raises}), len(calls)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
