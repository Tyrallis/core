#!/usr/bin/env python3
"""measure_layouts.py: struct sizes and field offsets, measured by compiling the client fork 32-bit.

Reads layouts.txt (the declared set of types and members), generates one C++ translation unit that
includes the fork's own headers and prints sizeof / offsetof for every entry, compiles it 32-bit
with the flags the fork's root CMakeLists.txt applies to its Win32 configuration, runs it, and
writes the X-macro table core/include/wxl/offsets/generated/layouts.def. Every entry that could not be measured goes to
layouts-unmeasured.txt with its reason, and every hand-written constant of the inventory in
audit-handwritten.md is compared with its measurement in layouts-compare.txt.

Python 3, standard library only. Needs CMake, a Visual Studio with the x86 MSVC toolchain, and the
Ninja that ships with Visual Studio (or one on PATH).
"""

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
CORE = os.path.dirname(os.path.dirname(HERE))

TOOL_NAME = 'core/tools/layouts/measure_layouts.py'


def vswhere_path():
    pf = os.environ.get('ProgramFiles(x86)') or os.environ.get('ProgramFiles')
    if not pf:
        raise Fail('neither ProgramFiles(x86) nor ProgramFiles is set; cannot locate vswhere.exe')
    return os.path.join(pf, 'Microsoft Visual Studio', 'Installer', 'vswhere.exe')


class Fail(Exception):
    pass


# ---------------------------------------------------------------------------------------------
# The hand-written inventory (audit-handwritten.md, section "Field-offset inventory")
# ---------------------------------------------------------------------------------------------

class HandRow:
    __slots__ = ('header', 'name', 'line', 'value', 'owner', 'comment', 'key')

    def __init__(self, header, name, line, value, owner, comment):
        self.header = header        # e.g. engine/GxDevice
        self.name = name            # e.g. kD3dDevice
        self.line = line            # line in the hand-written header
        self.value = value          # int
        self.owner = owner          # the section / owner hint column
        self.comment = comment      # the comment column
        self.key = None             # header:name, or header:name@line when the name repeats


def parse_inventory(path):
    """Returns {key: HandRow} for the 'Field-offset inventory' tables of the audit."""
    rows = []
    header = None
    in_inventory = False
    with open(path, encoding='utf-8') as f:
        for ln in f:
            if ln.startswith('## '):
                in_inventory = ln.startswith('## Field-offset inventory')
                continue
            if not in_inventory:
                continue
            m = re.match(r'### `client/warcraftxl/include/wxl/offsets/(.+)\.hpp`', ln)
            if m:
                header = m.group(1)
                continue
            m = re.match(r'\| (\d+) \| `(k\w+)` \| `(0x[0-9a-fA-F]+)` \| (.*?) \| (.*) \|\s*$', ln)
            if m and header:
                rows.append(HandRow(header, m.group(2), int(m.group(1)), int(m.group(3), 16),
                                    m.group(4).strip(), m.group(5).strip()))
    if not rows:
        raise Fail('no field-offset inventory rows found in %s' % path)
    counts = {}
    for r in rows:
        counts[(r.header, r.name)] = counts.get((r.header, r.name), 0) + 1
    by_key = {}
    for r in rows:
        r.key = '%s:%s' % (r.header, r.name)
        if counts[(r.header, r.name)] > 1:
            r.key += '@%d' % r.line
        if r.key in by_key:
            raise Fail('duplicate inventory key %s' % r.key)
        by_key[r.key] = r
    return by_key


# ---------------------------------------------------------------------------------------------
# layouts.txt
# ---------------------------------------------------------------------------------------------

class Ref:
    __slots__ = ('key', 'bias')

    def __init__(self, key, bias):
        self.key = key
        self.bias = bias


class Entry:
    __slots__ = ('kind', 'designator', 'refs', 'line', 'comment')

    def __init__(self, kind, designator, refs, line, comment):
        self.kind = kind                # 'size' | 'field' | 'msize'
        self.designator = designator    # '' for size
        self.refs = refs                # [Ref]
        self.line = line                # line in layouts.txt
        self.comment = comment


class Block:
    __slots__ = ('id', 'header', 'headers', 'cpp', 'entries', 'line', 'comment', 'failure')

    def __init__(self, id_, headers, cpp, line, comment):
        self.id = id_
        self.headers = headers      # the headers to include, in order; the first names the type's file
        self.header = headers[0]
        self.cpp = cpp
        self.entries = []
        self.line = line
        self.comment = comment
        self.failure = None     # compiler error text when the block could not be compiled


class Skip:
    __slots__ = ('refs', 'reason', 'line')

    def __init__(self, refs, reason, line):
        self.refs = refs
        self.reason = reason
        self.line = line


REF_RE = re.compile(r'^([\w/]+:k\w+(?:@\d+)?)(?:\+(0x[0-9a-fA-F]+|\d+))?$')


def parse_refs(token, where):
    if token == '-':
        return []
    refs = []
    for part in token.split(','):
        m = REF_RE.match(part)
        if not m:
            raise Fail('%s: bad hand reference %r' % (where, part))
        bias = int(m.group(2), 0) if m.group(2) else 0
        refs.append(Ref(m.group(1), bias))
    return refs


def split_comment(line):
    """Splits a layouts.txt line into (text, comment); '#' starts a comment unless inside <>."""
    depth = 0
    for i, ch in enumerate(line):
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif ch == '#' and depth <= 0:
            return line[:i].rstrip(), line[i + 1:].strip()
    return line.rstrip(), ''


def parse_layouts(path):
    blocks, skips = [], []
    ids = set()
    block = None
    with open(path, encoding='utf-8') as f:
        for n, raw in enumerate(f, 1):
            text, comment = split_comment(raw.rstrip('\n'))
            if not text.strip():
                continue
            where = '%s:%d' % (os.path.basename(path), n)
            if text.startswith('type '):
                parts = text.split(None, 3)
                if len(parts) < 4:
                    raise Fail('%s: expected "type <id> <header> <C++ type>"' % where)
                _, id_, header, cpp = parts
                if not re.match(r'^[A-Za-z_]\w*$', id_):
                    raise Fail('%s: id %r is not an identifier' % (where, id_))
                if id_ in ids:
                    raise Fail('%s: duplicate id %s' % (where, id_))
                ids.add(id_)
                headers = [h for h in header.replace('\\', '/').split(',') if h]
                block = Block(id_, headers, cpp.strip(), n, comment)
                blocks.append(block)
            elif text.startswith('skip '):
                body = text[5:]
                if '::' not in body:
                    raise Fail('%s: expected "skip <refs> :: <reason>"' % where)
                refs, reason = body.split('::', 1)
                refs = parse_refs(refs.strip(), where)
                if not refs:
                    raise Fail('%s: a skip line needs at least one hand reference' % where)
                skips.append(Skip(refs, reason.strip(), n))
            elif text.startswith((' ', '\t')):
                if block is None:
                    raise Fail('%s: entry before any type line' % where)
                parts = text.split()
                if len(parts) != 2:
                    raise Fail('%s: expected "<designator> <refs>" (two tokens)' % where)
                desig, refs = parts
                refs = parse_refs(refs, where)
                if desig == 'sizeof':
                    e = Entry('size', '', refs, n, comment)
                elif desig.startswith('sizeof(') and desig.endswith(')'):
                    e = Entry('msize', desig[7:-1], refs, n, comment)
                else:
                    e = Entry('field', desig, refs, n, comment)
                block.entries.append(e)
            else:
                raise Fail('%s: unrecognised line: %s' % (where, text))
    return blocks, skips


def reconcile_refs(blocks, skips, inventory, list_path):
    """Every inventory row is referenced exactly once, and every reference exists."""
    seen = {}
    problems = []

    def take(ref, where):
        if ref.key not in inventory:
            problems.append('%s: unknown hand constant %s' % (where, ref.key))
            return
        if ref.key in seen:
            problems.append('%s: %s already referenced at %s' % (where, ref.key, seen[ref.key]))
            return
        seen[ref.key] = where

    base = os.path.basename(list_path)
    for b in blocks:
        for e in b.entries:
            for r in e.refs:
                take(r, '%s:%d' % (base, e.line))
    for s in skips:
        for r in s.refs:
            take(r, '%s:%d' % (base, s.line))
    missing = sorted(k for k in inventory if k not in seen)
    for k in missing:
        row = inventory[k]
        problems.append('inventory row not referenced by %s: %s (line %d, %s)'
                        % (base, k, row.line, hex(row.value)))
    if problems:
        raise Fail('the declaration list does not reconcile with the inventory:\n  '
                   + '\n  '.join(problems))


# ---------------------------------------------------------------------------------------------
# The fork's compile flags, read from its root CMakeLists.txt
# ---------------------------------------------------------------------------------------------

def cmake_commands(text):
    """Yields (name, [args]) for every command of a CMake file, with quoting respected."""
    i, n = 0, len(text)
    while i < n:
        m = re.compile(r'(?m)^\s*([A-Za-z_]\w*)\s*\(').search(text, i)
        if not m:
            return
        name = m.group(1).lower()
        j = m.end()
        depth, args, cur, quoted = 1, [], '', False
        while j < n and depth > 0:
            ch = text[j]
            if quoted:
                if ch == '\\' and j + 1 < n:
                    cur += text[j + 1]
                    j += 2
                    continue
                if ch == '"':
                    quoted = False
                    args.append(cur)
                    cur = ''
                else:
                    cur += ch
            elif ch == '"':
                quoted = True
            elif ch == '#':
                while j < n and text[j] != '\n':
                    j += 1
            elif ch == '(':
                depth += 1
                cur += ch
            elif ch == ')':
                depth -= 1
                if depth > 0:
                    cur += ch
                elif cur:
                    args.append(cur)
                    cur = ''
            elif ch.isspace():
                if cur:
                    args.append(cur)
                    cur = ''
            else:
                cur += ch
            j += 1
        yield name, args
        i = j


class ForkFlags:
    def __init__(self):
        self.defines = []
        self.options = []
        self.cxx_standard = None
        self.conditions = []    # (condition text, value) for the summary


def evaluate_condition(args, options):
    """The conditions the fork's root CMakeLists.txt uses, evaluated for a Win32 MSVC build.

    Anything else raises, so a new condition upstream is noticed instead of silently mis-read.
    """
    cond = ' '.join(args)
    truth = {
        '${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR}': False,
        'NOT CMAKE_SYSTEM_NAME MATCHES Windows': False,
        'CMAKE_SIZEOF_VOID_P EQUAL 8': False,
        'CMAKE_SIZEOF_VOID_P EQUAL 4': True,
        'MSVC': True,
        'WIN32': True,
        'WOWDECOMP_ARCH_64': False,
        'WOWDECOMP_ARCH_32': True,
    }
    if cond in truth:
        return truth[cond]
    if cond in options:
        return options[cond]
    if args and args[0] in ('EXISTS', 'TARGET'):
        return False    # only add_subdirectory / target_sources sit under these
    raise Fail('unknown condition in the fork root CMakeLists.txt: if (%s); teach %s' % (cond, TOOL_NAME))


def read_fork_flags(client):
    path = os.path.join(client, 'CMakeLists.txt')
    with open(path, encoding='utf-8') as f:
        text = f.read()
    flags = ForkFlags()
    options = {}
    stack = []      # [active, any_branch_taken]

    def active():
        return all(s[0] for s in stack)

    for name, args in cmake_commands(text):
        if name == 'if':
            v = evaluate_condition(args, options) if active() else False
            flags.conditions.append(('if (%s)' % ' '.join(args), v))
            stack.append([v, v])
        elif name == 'elseif':
            top = stack[-1]
            parent_active = all(s[0] for s in stack[:-1])
            v = (not top[1]) and parent_active and evaluate_condition(args, options)
            flags.conditions.append(('elseif (%s)' % ' '.join(args), v))
            top[0] = v
            top[1] = top[1] or v
        elif name == 'else':
            top = stack[-1]
            top[0] = (not top[1]) and all(s[0] for s in stack[:-1])
            top[1] = True
        elif name == 'endif':
            stack.pop()
        elif not active():
            continue
        elif name == 'option' and len(args) >= 3:
            options[args[0]] = args[2].upper() in ('ON', 'TRUE', '1', 'YES')
        elif name == 'add_compile_definitions':
            flags.defines.extend(args)
        elif name == 'add_compile_options':
            flags.options.extend(args)
        elif name == 'set' and args and args[0] == 'CMAKE_CXX_STANDARD':
            flags.cxx_standard = args[1]
    if flags.cxx_standard is None:
        raise Fail('CMAKE_CXX_STANDARD not found in %s' % path)
    return flags


# ---------------------------------------------------------------------------------------------
# Toolchain
# ---------------------------------------------------------------------------------------------

class Toolchain:
    def __init__(self, vs_dir, env, ninja, cmake):
        self.vs_dir = vs_dir
        self.env = env
        self.ninja = ninja
        self.cmake = cmake


def find_toolchain():
    cmake = shutil.which('cmake')
    if not cmake:
        raise Fail('cmake not found on PATH')
    vswhere = vswhere_path()
    if not os.path.exists(vswhere):
        raise Fail('vswhere.exe not found at %s' % vswhere)
    r = subprocess.run([vswhere, '-latest', '-products', '*', '-requires',
                        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
                        '-property', 'installationPath'], capture_output=True, text=True)
    vs_dir = r.stdout.strip().splitlines()[0] if r.stdout.strip() else ''
    if not vs_dir:
        raise Fail('no Visual Studio with the MSVC x86/x64 toolchain found by vswhere')
    vcvars = os.path.join(vs_dir, 'VC', 'Auxiliary', 'Build', 'vcvars32.bat')
    if not os.path.exists(vcvars):
        raise Fail('vcvars32.bat not found: %s' % vcvars)
    # vcvars refuses some working directories (8.3 names in the path); run it from the drive root.
    if not os.environ.get('SystemDrive'):
        raise Fail('SystemDrive is not set')
    root = os.environ['SystemDrive'] + '\\'
    r = subprocess.run('cmd /s /c "call "%s" >nul && set"' % vcvars, capture_output=True,
                       text=True, cwd=root)
    env = {}
    for line in r.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    if env.get('VSCMD_ARG_TGT_ARCH') != 'x86':
        raise Fail('vcvars32.bat did not produce an x86 environment (VSCMD_ARG_TGT_ARCH=%r)'
                   % env.get('VSCMD_ARG_TGT_ARCH'))
    ninja = shutil.which('ninja', path=env.get('PATH'))
    if not ninja:
        cand = os.path.join(vs_dir, 'Common7', 'IDE', 'CommonExtensions', 'Microsoft', 'CMake',
                            'Ninja', 'ninja.exe')
        if os.path.exists(cand):
            ninja = cand
    if not ninja:
        raise Fail('ninja not found (neither on PATH nor in the Visual Studio CMake extension)')
    return Toolchain(vs_dir, env, ninja, cmake)


# ---------------------------------------------------------------------------------------------
# The dumper translation unit
# ---------------------------------------------------------------------------------------------

DUMPER_PRELUDE = r'''// GENERATED by %(tool)s from layouts.txt. Do not edit.
// Prints sizeof / offsetof of the client fork's types, compiled 32-bit with the fork's own flags.
#include <cstddef>
#include <cstdio>
#include <ctime>

%(includes)s

// The environment line proves the flags took effect: pointer size, time_t size (_USE_32BIT_TIME_T),
// the fork's own arch define, and _M_IX86_FP (0 under /arch:IA32).
static void dump_env() {
#if defined(WOWDECOMP_ARCH_32)
    const int arch32 = WOWDECOMP_ARCH_32;
#else
    const int arch32 = -1;
#endif
#if defined(_M_IX86_FP)
    const int ix86fp = _M_IX86_FP;
#else
    const int ix86fp = -1;
#endif
#if defined(_USE_32BIT_TIME_T)
    const int t32 = 1;
#else
    const int t32 = 0;
#endif
    std::printf("E\tptr=%%u\ttime_t=%%u\tuse_32bit_time_t=%%d\twowdecomp_arch_32=%%d\t_M_IX86_FP=%%d\t_MSC_VER=%%d\n",
                (unsigned)sizeof(void*), (unsigned)sizeof(time_t), t32, arch32, ix86fp, (int)_MSC_VER);
}

'''

DUMPER_MAIN = r'''
int main() {
    dump_env();
%(calls)s
    return 0;
}
'''


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def generate_dumper(blocks):
    includes = []
    for b in blocks:
        for h in b.headers:
            inc = '#include "%s"' % h
            if inc not in includes:
                includes.append(inc)
    out = [DUMPER_PRELUDE % {'tool': TOOL_NAME, 'includes': '\n'.join(includes)}]
    calls = []
    for n, b in enumerate(blocks):
        lines = ['// layouts.txt:%d  %s' % (b.line, b.id),
                 'static void dump_%d() {' % n,
                 '    typedef %s T;' % b.cpp,
                 '    std::printf("T\\t%%s\\t%%u\\n", %s, (unsigned)sizeof(T));' % c_string(b.id)]
        for e in b.entries:
            if e.kind == 'field':
                lines.append('    std::printf("F\\t%%s\\t%%s\\t%%u\\n", %s, %s, (unsigned)offsetof(T, %s));'
                             % (c_string(b.id), c_string(e.designator), e.designator))
            elif e.kind == 'msize':
                lines.append('    std::printf("S\\t%%s\\t%%s\\t%%u\\n", %s, %s, (unsigned)sizeof(((T*)0)->%s));'
                             % (c_string(b.id), c_string(e.designator), e.designator))
        lines.append('}')
        out.append('\n'.join(lines) + '\n')
        calls.append('    dump_%d();' % n)
    out.append(DUMPER_MAIN % {'calls': '\n'.join(calls)})
    return '\n'.join(out)


# ---------------------------------------------------------------------------------------------
# Build and run
# ---------------------------------------------------------------------------------------------

def run(cmd, env, cwd=None, shell=False):
    r = subprocess.run(cmd, env=env, cwd=cwd, capture_output=True, text=True, shell=shell,
                       encoding='utf-8', errors='replace')
    return r.returncode, r.stdout + r.stderr


def configure(tc, client, build, flags, dumper_path, includes, config, log):
    os.makedirs(build, exist_ok=True)
    cmd = [tc.cmake, '-S', HERE, '-B', build, '-G', 'Ninja',
           '-DCMAKE_MAKE_PROGRAM=' + tc.ninja,
           '-DCMAKE_BUILD_TYPE=' + config,
           '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
           '-DWXL_CLIENT_DIR=' + client.replace('\\', '/'),
           '-DWXL_DUMPER_SOURCE=' + dumper_path.replace('\\', '/'),
           '-DWXL_FORK_CXX_STANDARD=' + flags.cxx_standard,
           '-DWXL_FORK_DEFINES=' + ';'.join(flags.defines),
           '-DWXL_FORK_OPTIONS=' + ';'.join(flags.options),
           '-DWXL_FORK_INCLUDES=' + ';'.join(includes)]
    code, out = run(cmd, tc.env)
    log.append('$ ' + ' '.join(cmd) + '\n' + out)
    if code != 0:
        raise Fail('cmake configure failed:\n' + out)


def build_target(tc, build, log):
    code, out = run([tc.ninja, '-C', build, '-v', 'layout_dumper'], tc.env)
    log.append('$ ninja -C %s -v layout_dumper\n%s' % (build, out))
    return code == 0, out


def compile_command(build, dumper_path):
    """The dumper's own compile command, as CMake generated it (compile_commands.json)."""
    with open(os.path.join(build, 'compile_commands.json'), encoding='utf-8') as f:
        cc = json.load(f)
    want = os.path.normcase(os.path.abspath(dumper_path))
    for entry in cc:
        if os.path.normcase(os.path.abspath(entry['file'])) == want:
            return entry['command']
    raise Fail('the dumper is not in compile_commands.json')


def try_compile_subset(tc, command, dumper_path, subset, scratch, tag):
    """Compiles a dumper built from `subset` with the exact command CMake generated, no link."""
    src = os.path.join(scratch, 'probe_%s.cpp' % tag)
    obj = os.path.join(scratch, 'probe_%s.obj' % tag)
    with open(src, 'w', encoding='utf-8', newline='\n') as f:
        f.write(generate_dumper(subset))
    # Swap the source and the object; keep every other flag verbatim.
    cmd = command
    cmd = re.sub(r'/Fo\S+', '/Fo' + obj.replace('\\', '/'), cmd)
    cmd = re.sub(r'(?i)\s/Fd\S+', '', cmd)
    cmd = cmd.replace(dumper_path.replace('\\', '/'), src.replace('\\', '/'))
    cmd = cmd.replace(dumper_path, src)
    code, out = run(cmd, tc.env, shell=True)
    return code == 0, out


def first_error(out):
    for ln in out.splitlines():
        if re.search(r'\b(error|fatal error) [A-Z]\d{4}:', ln):
            return ln.strip()
    return (out.strip().splitlines() or ['compile failed'])[-1]


def bisect_failures(tc, command, dumper_path, blocks, scratch, log):
    """Finds the blocks that do not compile. Returns the list of blocks that do."""
    ok = []
    counter = [0]

    def rec(subset):
        counter[0] += 1
        good, out = try_compile_subset(tc, command, dumper_path, subset, scratch, str(counter[0]))
        log.append('probe %d: %d block(s) %s' % (counter[0], len(subset), 'ok' if good else 'FAILED'))
        if good:
            ok.extend(subset)
            return
        if len(subset) == 1:
            subset[0].failure = first_error(out)
            log.append('  ' + subset[0].failure)
            return
        half = len(subset) // 2
        rec(subset[:half])
        rec(subset[half:])

    rec(list(blocks))
    return [b for b in blocks if b.failure is None]


def run_dumper(tc, build, log):
    exe = os.path.join(build, 'layout_dumper.exe')
    code, out = run([exe], tc.env)
    log.append('$ ' + exe + '\n' + out)
    if code != 0:
        raise Fail('the dumper exited with %d:\n%s' % (code, out))
    env_line = None
    types, fields, msizes = {}, {}, {}
    for ln in out.splitlines():
        parts = ln.split('\t')
        if parts[0] == 'E':
            env_line = dict(p.split('=', 1) for p in parts[1:])
        elif parts[0] == 'T':
            types[parts[1]] = int(parts[2])
        elif parts[0] == 'F':
            fields[(parts[1], parts[2])] = int(parts[3])
        elif parts[0] == 'S':
            msizes[(parts[1], parts[2])] = int(parts[3])
    if env_line is None:
        raise Fail('the dumper printed no environment line')
    return env_line, types, fields, msizes


# ---------------------------------------------------------------------------------------------
# Outputs
# ---------------------------------------------------------------------------------------------

DEF_HEADER = '''// {title}
//
// GENERATED by {tool} from layouts.txt, by compiling the client fork's headers 32-bit.
// Do not edit: edit layouts.txt or the fork, re-run the tool and review the diff.
//
// Three X-macros:
//   WXL_LAYOUT_TYPE(id, type, size, file, line)       sizeof(type)
//   WXL_LAYOUT_FIELD(id, member, offset, file, line)  offsetof(type, member)
//   WXL_LAYOUT_SIZE(id, member, size, file, line)     sizeof(type::member), for strides and byte sizes
// id is a plain identifier chosen in layouts.txt; every other text column is a C string literal.
// file/line locate the type's declaration in the client checkout (line 0 = not found by name).
// Rows are sorted by id, then TYPE, then FIELD by offset and member, then SIZE by member, so the
// file diffs cleanly after an upstream pull.
//
// {types} types, {fields} field offsets, {sizes} member sizes.
'''


def decl_line(client, headers, cpp):
    """Best-effort (header, line) of the type's declaration among the block's headers."""
    name = re.sub(r'<.*', '', cpp).strip()
    name = name.split('::')[-1].strip()
    for header in headers:
        path = os.path.join(client, header)
        try:
            with open(path, encoding='utf-8', errors='replace') as f:
                for n, ln in enumerate(f, 1):
                    if re.match(r'^\s*(class|struct)\s+' + re.escape(name) + r'\b', ln) and not ln.rstrip().endswith(';'):
                        return header, n
        except OSError:
            pass
    return headers[0], 0


def write_def(path, blocks, types, fields, msizes, client):
    rows = []
    for b in blocks:
        if b.failure is not None or b.id not in types:
            continue
        header, line = decl_line(client, b.headers, b.cpp)
        rows.append((b.id, 0, 0, '', 'WXL_LAYOUT_TYPE(%s, %s, 0x%X, %s, %d)'
                     % (b.id, c_string(b.cpp), types[b.id], c_string(header), line)))
        for e in b.entries:
            if e.kind == 'field':
                off = fields[(b.id, e.designator)]
                rows.append((b.id, 1, off, e.designator, 'WXL_LAYOUT_FIELD(%s, %s, 0x%X, %s, %d)'
                             % (b.id, c_string(e.designator), off, c_string(header), line)))
            elif e.kind == 'msize':
                sz = msizes[(b.id, e.designator)]
                rows.append((b.id, 2, 0, e.designator, 'WXL_LAYOUT_SIZE(%s, %s, 0x%X, %s, %d)'
                             % (b.id, c_string(e.designator), sz, c_string(header), line)))
    rows.sort()
    n_types = sum(1 for r in rows if r[1] == 0)
    n_fields = sum(1 for r in rows if r[1] == 1)
    n_sizes = sum(1 for r in rows if r[1] == 2)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(DEF_HEADER.format(
            title='Struct sizes and field offsets of Wow.exe 3.3.5a (12340), measured on the client fork.',
            tool=TOOL_NAME, types=n_types, fields=n_fields, sizes=n_sizes))
        for r in rows:
            f.write(r[4] + '\n')
    return n_types, n_fields, n_sizes


class Comparison:
    __slots__ = ('row', 'status', 'measured', 'bias', 'where', 'note')

    def __init__(self, row, status, measured, bias, where, note):
        self.row = row
        self.status = status        # OK | MISMATCH | SKIPPED | FAILED
        self.measured = measured    # int or None (already includes the bias)
        self.bias = bias
        self.where = where          # "Type::member" / "sizeof(Type)" / ...
        self.note = note            # skip reason or compiler error


def compare(blocks, skips, inventory, types, fields, msizes):
    comps = {}
    for b in blocks:
        for e in b.entries:
            if e.kind == 'size':
                where = 'sizeof(%s)' % b.cpp
                value = types.get(b.id)
            elif e.kind == 'msize':
                where = 'sizeof(%s::%s)' % (b.cpp, e.designator)
                value = msizes.get((b.id, e.designator))
            else:
                where = '%s::%s' % (b.cpp, e.designator)
                value = fields.get((b.id, e.designator))
            for r in e.refs:
                row = inventory[r.key]
                if b.failure is not None or value is None:
                    comps[r.key] = Comparison(row, 'FAILED', None, r.bias, where,
                                              b.failure or 'not measured')
                    continue
                measured = value + r.bias
                status = 'OK' if measured == row.value else 'MISMATCH'
                comps[r.key] = Comparison(row, status, measured, r.bias, where, e.comment)
    for s in skips:
        for r in s.refs:
            comps[r.key] = Comparison(inventory[r.key], 'SKIPPED', None, 0, '', s.reason)
    return comps


def row_sort_key(c):
    return (c.row.header, c.row.line, c.row.name)


def write_compare(path, comps):
    items = sorted(comps.values(), key=row_sort_key)
    counts = {}
    for c in items:
        counts[c.status] = counts.get(c.status, 0) + 1
    mismatches = [c for c in items if c.status == 'MISMATCH']
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Hand-written field-offset constants (client/warcraftxl/include/wxl/offsets, as inventoried\n')
        f.write('# in audit-handwritten.md) against the 32-bit measurement of the client fork.\n')
        f.write('# GENERATED by %s. A MISMATCH says the decompiled struct and the hand-written\n' % TOOL_NAME)
        f.write('# constant differ; either side can be the wrong one.\n#\n')
        f.write('# rows %d: OK %d, MISMATCH %d, SKIPPED %d, FAILED %d\n\n'
                % (len(items), counts.get('OK', 0), counts.get('MISMATCH', 0),
                   counts.get('SKIPPED', 0), counts.get('FAILED', 0)))
        f.write('## MISMATCH (%d)\n\n' % len(mismatches))
        for c in mismatches:
            f.write('%s.hpp:%d  %s\n    hand 0x%X  measured 0x%X%s  (%s)\n    hand comment: %s\n'
                    % (c.row.header, c.row.line, c.row.name, c.row.value, c.measured,
                       ' (incl. +0x%X)' % c.bias if c.bias else '', c.where,
                       c.row.comment or '-'))
        f.write('\n## All rows\n\n')
        f.write('# status  hand-header:line  name  hand  measured  mapped-to  [note]\n')
        header = None
        for c in items:
            if c.row.header != header:
                header = c.row.header
                f.write('\n### %s.hpp\n' % header)
            measured = '0x%X' % c.measured if c.measured is not None else '-'
            bias = ' (+0x%X)' % c.bias if c.bias else ''
            note = ('  [%s]' % c.note) if c.note else ''
            f.write('%-8s %4d  %-36s hand 0x%-6X measured %-8s %s%s%s\n'
                    % (c.status, c.row.line, c.row.name, c.row.value, measured, c.where, bias, note))
    return counts


def write_unmeasured(path, blocks, comps):
    failed_blocks = [b for b in blocks if b.failure is not None]
    skipped = sorted((c for c in comps.values() if c.status == 'SKIPPED'), key=row_sort_key)
    failed = sorted((c for c in comps.values() if c.status == 'FAILED'), key=row_sort_key)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# Entries of layouts.txt and hand-written constants of the inventory that are NOT in\n')
        f.write('# layouts.def, each with its reason. GENERATED by %s.\n' % TOOL_NAME)
        f.write('# A type that failed to compile lists the compiler\'s first error; a skipped hand constant\n')
        f.write('# lists the reason written in layouts.txt.\n\n')
        f.write('## Types that failed to compile (%d)\n\n' % len(failed_blocks))
        for b in failed_blocks:
            f.write('%s  (%s, layouts.txt:%d)\n    %s\n' % (b.id, b.cpp, b.line, b.failure))
        f.write('\n## Hand constants without a measurement because their type failed (%d)\n\n' % len(failed))
        for c in failed:
            f.write('%s.hpp:%d  %s = 0x%X  -> %s\n' % (c.row.header, c.row.line, c.row.name, c.row.value, c.where))
        f.write('\n## Hand constants skipped on purpose (%d)\n\n' % len(skipped))
        for c in skipped:
            f.write('%s.hpp:%d  %s = 0x%X\n    %s\n' % (c.row.header, c.row.line, c.row.name, c.row.value, c.note))
    return len(failed_blocks), len(failed), len(skipped)


def write_summary(path, info):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('Layout measurement summary\n==========================\n')
        for k, v in info:
            if v is None:
                f.write('\n%s\n%s\n' % (k, '-' * len(k)))
            else:
                f.write('%-28s %s\n' % (k, v))


# ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('client', help='path to the client checkout (read-only input)')
    ap.add_argument('--list', default=os.path.join(HERE, 'layouts.txt'),
                    help='the declaration list (default: layouts.txt next to this script)')
    ap.add_argument('--audit', default=os.path.join(CORE, 'docs', 'offsets-audit-2026-10-17.md'),
                    help='the audit whose field-offset inventory is compared (default: core/docs/offsets-audit-2026-10-17.md)')
    ap.add_argument('--out', default=os.path.join(CORE, 'include', 'wxl', 'offsets', 'generated'),
                    help='where layouts.def is written (default: core/include/wxl/offsets/generated)')
    ap.add_argument('--reports', default=os.path.join(CORE, 'reports'),
                    help='where the compare/summary/unmeasured reports go (default: core/reports)')
    ap.add_argument('--build', default=os.path.join(HERE, 'build'),
                    help='build directory for the dumper (default: tools/layouts/build, not committed)')
    ap.add_argument('--include', action='append', default=[],
                    help='extra include directory relative to the checkout (repeatable)')
    ap.add_argument('--config', default='Release', choices=['Release', 'Debug', 'RelWithDebInfo', 'MinSizeRel'],
                    help='CMAKE_BUILD_TYPE of the dumper (default: Release; the layouts do not depend on it)')
    ap.add_argument('--quiet', action='store_true', help='do not print the summary')
    args = ap.parse_args()

    t0 = time.time()
    client = os.path.abspath(args.client)
    if not os.path.exists(os.path.join(client, 'CMakeLists.txt')):
        raise Fail('not a client checkout (no CMakeLists.txt): %s' % client)
    inventory = parse_inventory(args.audit)
    blocks, skips = parse_layouts(args.list)
    reconcile_refs(blocks, skips, inventory, args.list)
    flags = read_fork_flags(client)
    tc = find_toolchain()

    build = os.path.abspath(args.build)
    os.makedirs(build, exist_ok=True)
    scratch = os.path.join(build, 'probe')
    shutil.rmtree(scratch, ignore_errors=True)
    os.makedirs(scratch, exist_ok=True)
    dumper_path = os.path.join(build, 'layout_dumper.cpp')
    log = []

    with open(dumper_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(generate_dumper(blocks))
    t1 = time.time()
    configure(tc, client, build, flags, dumper_path, args.include, args.config, log)
    t2 = time.time()
    ok, out = build_target(tc, build, log)
    command = compile_command(build, dumper_path)
    probes = 0
    if not ok:
        # Find the blocks that do not compile, drop them, build the rest.
        before = len(log)
        compiled = bisect_failures(tc, command, dumper_path, blocks, scratch, log)
        probes = sum(1 for l in log[before:] if l.startswith('probe '))
        if not compiled:
            raise Fail('no type of layouts.txt compiles; first error:\n' + first_error(out))
        with open(dumper_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write(generate_dumper(compiled))
        ok, out = build_target(tc, build, log)
        if not ok:
            raise Fail('the dumper still fails to build after dropping the failing types:\n' + out)
    t3 = time.time()
    env_line, types, fields, msizes = run_dumper(tc, build, log)
    t4 = time.time()

    if env_line.get('ptr') != '4':
        raise Fail('the dumper is not 32-bit: %r' % env_line)

    os.makedirs(args.out, exist_ok=True)
    os.makedirs(args.reports, exist_ok=True)
    n_types, n_fields, n_sizes = write_def(os.path.join(args.out, 'layouts.def'), blocks, types, fields, msizes, client)
    comps = compare(blocks, skips, inventory, types, fields, msizes)
    counts = write_compare(os.path.join(args.reports, 'layouts-compare.txt'), comps)
    n_failed_blocks, n_failed_rows, n_skipped = write_unmeasured(
        os.path.join(args.reports, 'layouts-unmeasured.txt'), blocks, comps)

    measured_rows = counts.get('OK', 0) + counts.get('MISMATCH', 0)
    total = len(inventory)
    if measured_rows + n_skipped + n_failed_rows != total:
        raise Fail('reconciliation failed: %d measured + %d skipped + %d failed != %d'
                   % (measured_rows, n_skipped, n_failed_rows, total))

    # No absolute paths in the committed summary: the checkout, the build directory and the
    # Visual Studio root are written as placeholders.
    def portable(s):
        for value, token in ((client, '<client>'), (build, '<build>'), (tc.vs_dir, '<vs>')):
            for v in (value.replace('\\', '/'), value.replace('/', '\\')):
                s = re.sub(re.escape(v), token, s, flags=re.IGNORECASE)
        return s

    info = [
        ('client checkout', portable(client) + ' (' + os.path.basename(client) + ')'),
        ('declaration list', os.path.relpath(args.list, CORE).replace('\\', '/')),
        ('types declared', len(blocks)),
        ('types measured', n_types),
        ('types failed to compile', n_failed_blocks),
        ('field offsets measured', n_fields),
        ('member sizes measured', n_sizes),
        ('Hand-written inventory', None),
        ('inventory rows', total),
        ('compared OK', counts.get('OK', 0)),
        ('compared MISMATCH', counts.get('MISMATCH', 0)),
        ('skipped (with reason)', n_skipped),
        ('failed (type did not compile)', n_failed_rows),
        ('reconciliation', '%d = %d + %d + %d' % (total, measured_rows, n_skipped, n_failed_rows)),
        ('Compiler', None),
        ('build type', args.config),
        ('cxx standard', flags.cxx_standard),
        ('fork defines', ' '.join(flags.defines)),
        ('fork options', ' '.join(flags.options)),
        ('fork conditions', '; '.join('%s -> %s' % c for c in flags.conditions)),
        ('dumper environment', '  '.join('%s=%s' % kv for kv in env_line.items())),
        ('compile command', portable(command)),
        ('Timing', None),
        ('generate', '%.1fs' % (t1 - t0)),
        ('configure', '%.1fs' % (t2 - t1)),
        ('build (+%d probes)' % probes, '%.1fs' % (t3 - t2)),
        ('run + write', '%.1fs' % (time.time() - t3)),
        ('total', '%.1fs' % (time.time() - t0)),
    ]
    write_summary(os.path.join(args.reports, 'layouts-summary.txt'), info)
    with open(os.path.join(build, 'measure.log'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n\n'.join(log))
    if not args.quiet:
        with open(os.path.join(args.reports, 'layouts-summary.txt'), encoding='utf-8') as f:
            sys.stdout.write(f.read())
        if counts.get('MISMATCH'):
            print('\n%d MISMATCH row(s): see %s' % (counts['MISMATCH'], os.path.join(args.reports, 'layouts-compare.txt')))
        if n_failed_blocks:
            print('%d type(s) failed to compile: see %s' % (n_failed_blocks, os.path.join(args.reports, 'layouts-unmeasured.txt')))


if __name__ == '__main__':
    try:
        main()
    except Fail as e:
        sys.stderr.write('measure_layouts: %s\n' % e)
        sys.exit(1)
