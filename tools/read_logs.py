#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 WarcraftXL
"""Summarises the last launches of a client from its WarcraftXL logs.

    python3 tools/read_logs.py <client folder | Logs folder | one .log file> [-n 3] [--lines 6]

For each launch, oldest first: the header (when, which runtime, built from which commits, the
executable, Windows and Wine, the machine), the mods found, loaded and refused, the stages the
client reached, how it ended (a clean exit, a crash with its report, or no end line at all), the
warnings and errors grouped by what they say, the marks set with the wxl.probe mod's /mark and
what happened between them, and the events the probe saw and never saw. Then the newest files of
the client's own Errors/ folder.

The runtime writes Logs/WarcraftXL.log for the running launch and renames the previous one
WarcraftXL-<its launch time>.log, so the files sort by age by name.
"""

import argparse
import re
import sys
from datetime import datetime
from pathlib import Path

LINE = re.compile(r"^\[(\d\d:\d\d:\d\d\.\d{3})\] \[(TRACE|DEBUG|INFO |WARN |ERROR)\] \[([^\]]*)\] ?(.*)$")
BARE = re.compile(r"^\[(TRACE|DEBUG|INFO |WARN |ERROR)\] \[([^\]]*)\] ?(.*)$")
MARK = re.compile(r"^==== MARK (\d+): (.*) ====$")
NUMBERS = re.compile(r"0x[0-9A-Fa-f]+|\b\d+(\.\d+)?\b")


class Entry:
    __slots__ = ("time", "level", "tag", "text", "index")

    def __init__(self, time, level, tag, text, index):
        self.time, self.level, self.tag, self.text, self.index = time, level.strip(), tag, text, index


def parse(path):
    """The entries of one file, a continuation line (four spaces of indent) joined to its own."""
    entries = []
    with open(path, "r", encoding="utf-8", errors="replace") as stream:
        for raw in stream:
            raw = raw.rstrip("\r\n")
            match = LINE.match(raw)
            if match:
                entries.append(Entry(match[1], match[2], match[3], match[4], len(entries)))
                continue
            match = BARE.match(raw)
            if match:
                entries.append(Entry("", match[1], match[2], match[3], len(entries)))
                continue
            if entries and raw.strip():
                entries[-1].text += "\n" + raw[4:] if raw.startswith("    ") else "\n" + raw
    return entries


def files_of(target):
    """The log files of a client folder, a Logs folder or one file, oldest first."""
    target = Path(target)
    if target.is_file():
        return [target]
    logs = target / "Logs" if (target / "Logs").is_dir() else target
    archived = sorted(logs.glob("WarcraftXL-*.log"))
    current = logs / "WarcraftXL.log"
    return archived + ([current] if current.exists() else [])


def first(entries, tag, prefix):
    for entry in entries:
        if entry.tag == tag and entry.text.startswith(prefix):
            return entry
    return None


def shorten(text, width):
    text = text.replace("\n", " | ")
    return text if len(text) <= width else text[: width - 3] + "..."


def group_problems(entries):
    """Warnings and errors, grouped by tag and wording with the numbers taken out."""
    groups = {}
    for entry in entries:
        if entry.level not in ("WARN", "ERROR") or entry.tag == "crash":
            continue
        key = (entry.level, entry.tag, NUMBERS.sub("#", entry.text.split("\n")[0]))
        group = groups.setdefault(key, [0, entry])
        group[0] += 1
    return sorted(groups.items(), key=lambda item: (item[0][0] != "ERROR", item[1][1].index))


def summarise(path, entries, args, out):
    say = lambda text="": out.append(text)
    launch = first(entries, "session", "launch ")
    say("=" * 100)
    say(f"{path.name}: {launch.text if launch else 'no launch header (an older runtime wrote this file)'}")
    say("=" * 100)

    header = [e for e in entries if e.tag == "session" and not e.text.startswith(("mod ", "mods:", "session end"))]
    for entry in header[1:]:
        say(f"  {shorten(entry.text, 220)}")

    # --- mods ---------------------------------------------------------------------------------
    mods = [e for e in entries if e.tag == "session" and e.text.startswith(("mods:", "mod "))]
    lua = [e for e in entries if e.tag == "wxl.lua" and re.match(
        r"(loaded |refused |mod \S+ (failed to load|is disabled|stays unloaded)|\d+ script mod)", e.text)]
    disabled = [e for e in entries if e.tag == "host" and " it is disabled" in e.text]
    if mods or lua or disabled:
        say()
        say("Mods")
        for entry in mods:
            say(f"  {shorten(entry.text, 220)}")
        for entry in lua:
            say(f"  [wxl.lua] {shorten(entry.text, 210)}")
        for entry in disabled:
            say(f"  {entry.time} [host] {shorten(entry.text, 200)}")

    # --- stages and the end -------------------------------------------------------------------
    stages = [e for e in entries if e.tag == "core" and e.text.startswith("stage ")]
    if stages:
        say()
        say("Stages  " + ", ".join(f"{e.text[6:]} {e.time}" for e in stages))

    say()
    end = first(entries, "session", "session end")
    crash = [e for e in entries if e.tag == "crash" and not e.text.startswith("the crash report runs first")]
    if crash:
        say(f"CRASH at {crash[0].time}")
        for entry in crash:
            say(f"  {shorten(entry.text, 240)}")
        if end and "crash" not in end.text:
            say(f"  (after the session's end: {end.text})")
    if end:
        say(f"End     {end.time} {end.text}")
    else:
        last = entries[-1] if entries else None
        say("End     NO END LINE: the process ended without a word (killed, frozen and closed, or a crash "
            "nothing could report)" + (f"; the last line is at {last.time}: [{last.tag}] {shorten(last.text, 120)}"
                                        if last else ""))

    # --- warnings and errors ------------------------------------------------------------------
    problems = group_problems(entries)
    say()
    if not problems:
        say("Warnings and errors: none")
    else:
        warns = sum(g[0] for (lvl, _, _), g in problems if lvl == "WARN")
        errors = sum(g[0] for (lvl, _, _), g in problems if lvl == "ERROR")
        say(f"Warnings and errors: {errors} error(s), {warns} warning(s), in {len(problems)} kinds")
        for (level, tag, _), (count, sample) in problems:
            times = f" x{count}" if count > 1 else ""
            say(f"  {level:5} {sample.time} [{tag}]{times} {shorten(sample.text, 200)}")

    # --- marks ---------------------------------------------------------------------------------
    marks = [(e, MARK.match(e.text)) for e in entries if e.tag == "wxl.probe" and MARK.match(e.text)]
    if marks:
        say()
        say(f"Marks ({len(marks)})")
        previous = 0
        for entry, match in marks:
            between = [e for e in entries[previous:entry.index] if e.level in ("WARN", "ERROR")]
            say(f"  {entry.time}  MARK {match[1]}: {match[2]}")
            for follow in entries[entry.index + 1: entry.index + 4]:
                if follow.tag == "wxl.probe" and follow.text.startswith(("since ", "memory at mark")):
                    say(f"      {shorten(follow.text, 200)}")
            if between:
                say(f"      before it: {len(between)} warning(s)/error(s), first: "
                    f"[{between[0].tag}] {shorten(between[0].text, 120)}")
            previous = entry.index
        tail = [e for e in entries[previous:] if e.level in ("WARN", "ERROR")]
        if tail:
            say(f"  after the last mark: {len(tail)} warning(s)/error(s)")

    # --- events --------------------------------------------------------------------------------
    probe = [e for e in entries if e.tag == "wxl.probe"]
    if probe:
        say()
        say("Events (wxl.probe)")
        listening = next((e for e in probe if e.text.startswith("listening to")), None)
        if listening:
            say(f"  {listening.text}")
        for prefix in ("not raised by this runtime", "not listened to", "summary:", "never seen"):
            parts = [e.text for e in probe if e.text.startswith(prefix)]
            for text in parts:
                say(f"  {shorten(text, args.width)}")
        if not any(e.text.startswith("summary:") for e in probe):
            seen = sorted({m[1] for e in probe for m in [re.match(r"(On\w+) #1", e.text)] if m})
            say(f"  no summary (the probe's onShutdown did not run: a crash, or a process that ended before "
                f"the client closed); seen at least once: {len(seen)}: "
                + shorten(", ".join(seen), args.width))
        minutes = [e for e in probe if e.text.startswith("minute ")]
        if minutes:
            say(f"  {len(minutes)} minute line(s); the last: {shorten(minutes[-1].text, 160)}")
        firsts = [e for e in probe if re.match(r"On\w+ #1:", e.text)]
        if args.lines and firsts:
            say(f"  first occurrences ({len(firsts)}, showing {min(args.lines, len(firsts))}):")
            for entry in firsts[: args.lines]:
                say(f"    {entry.time} {shorten(entry.text, 160)}")


def errors_folder(target, out):
    errors = Path(target) / "Errors"
    if not errors.is_dir():
        return
    files = sorted((p for p in errors.iterdir() if p.is_file()), key=lambda p: p.stat().st_mtime)
    out.append("")
    out.append(f"Errors/ of the client: {len(files)} file(s)" + (", the newest:" if files else ""))
    for p in files[-5:]:
        stamp = datetime.fromtimestamp(p.stat().st_mtime).strftime("%Y-%m-%d %H:%M:%S")
        out.append(f"  {stamp}  {p.name}  ({p.stat().st_size} bytes)")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("target", help="a client folder (tlk_x86, tlk_x64), its Logs folder, or one .log file")
    parser.add_argument("-n", "--launches", type=int, default=3, help="how many of the last launches (default 3)")
    parser.add_argument("--lines", type=int, default=6, help="first occurrences of events to show (default 6)")
    parser.add_argument("--width", type=int, default=400, help="longest list line shown (default 400)")
    args = parser.parse_args()

    files = files_of(args.target)
    if not files:
        print(f"no WarcraftXL log under {args.target}", file=sys.stderr)
        return 1
    out = []
    chosen = files[-args.launches:] if args.launches > 0 else files
    out.append(f"{len(files)} launch log(s) under {args.target}; the last {len(chosen)}, oldest first")
    for path in chosen:
        summarise(path, parse(path), args, out)
        out.append("")
    if Path(args.target).is_dir():
        errors_folder(args.target, out)
    print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
