#!/usr/bin/env python3
# Derive core's address tables from the OFFSET:/STATIC: tags of the client fork.
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
"""
Scan a `client` checkout (the C++ decompilation of Wow.exe 3.3.5a build 12340) for the
comment tags that give every reconstructed function and static its address in the original
binary, and write them out as X-macro tables for `core`.

    // VERIFY: 271a03c4 manual
    // OFFSET: 0x0068FA60
    void CGxDeviceD3d::IBufUnlock(CGxBuf* buf) {

    // STATIC: 0x00C60930
    static BLIT_FUNCTION s_blits[BlitFormats_Last][BlitFormats_Last][BlitAlphas_Last];

Outputs (in the output directory):
    functions.def   one row per function address
    statics.def     one row per static address
    unparsed.txt    every OFFSET:/STATIC: tag the tool could not confidently name
    summary.txt     counts, coverage and anomalies

Invariant: every OFFSET:/STATIC: tag ends up either as rows or as an unparsed entry, and the
tool refuses to write anything if the totals do not reconcile.

Python 3, standard library only.
"""

import argparse
import collections
import os
import re
import sys

SOURCE_EXTENSIONS = ('.cpp', '.c', '.h', '.hpp', '.inl', '.cc', '.cxx')
HEADER_EXTENSIONS = ('.h', '.hpp', '.inl')
SKIP_DIRS = {'.git', '.svn', '.hg'}

TAG_RE = re.compile(r'^\s*//\s*(OFFSET|STATIC|VERIFY|INLINED|NOTE):\s*(.*?)\s*$')
HEX_RE = re.compile(r'^0x[0-9A-Fa-f]{1,8}$')
VERIFY_RE = re.compile(r'^([0-9A-Fa-f]{8})\s+(auto|manual)$')

# Lines after the tag that may precede the declaration proper.
TEMPLATE_PREFIX_RE = re.compile(r'^\s*template\s*<')
ACCESS_LABEL_RE = re.compile(r'^\s*(?:public|private|protected)\s*:')

# A name whose last segment is one of these is not a function or variable name.
NOT_A_NAME = {
    'if', 'for', 'while', 'switch', 'return', 'sizeof', 'catch', 'defined', 'new', 'delete',
    'throw', 'typeid', 'alignof', 'decltype', 'static_assert', 'else', 'do', 'case', 'goto',
    '__try', '__except', '__finally', '__leave', 'try', 'using', 'typedef',
    'void', 'int', 'char', 'short', 'long', 'float', 'double', 'bool', 'unsigned', 'signed',
    'const', 'volatile', 'static', 'extern', 'inline', 'register', 'struct', 'class', 'union',
    'enum', 'auto', 'template', 'typename', 'operator', 'namespace', 'virtual', 'explicit',
    'friend', 'mutable', 'constexpr', 'wchar_t', 'size_t', '__int64', '__declspec',
    '__cdecl', '__stdcall', '__fastcall', '__thiscall', '__forceinline', '__inline',
}
CONTROL_HEAD = ('if', 'for', 'while', 'switch', 'else', 'do', 'try', 'catch', 'return',
                '__try', '__except', '__finally', 'case', 'default')

# What may follow a function's parameter list before its body or the end of the declaration.
PARAM_TAIL_RE = re.compile(
    r'^\s*(?:(?:const|volatile|noexcept|override|final|&&|&'
    r'|throw\s*\([^)]*\)|__attribute__\s*\(\(.*?\)\)|=\s*0|=\s*default|=\s*delete)\s*)*'
    r'(?::(?!:).*)?$', re.S)


def fail(message):
    sys.stderr.write('extract_offsets: ' + message + '\n')
    sys.exit(2)


# ----------------------------------------------------------------------------------------------
# Masking: a same-length view of the file where comments, string and character literals and
# preprocessor lines are blanked, so brace and parenthesis scanning sees code only.
# ----------------------------------------------------------------------------------------------

def mask_source(text):
    out = []
    i = 0
    n = len(text)
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start:
            j = i
            while j < n and text[j] in ' \t':
                j += 1
            if j < n and text[j] == '#':
                # Preprocessor line, with its continuation lines.
                k = j
                while k < n:
                    if text[k] == '\n':
                        if k > 0 and text[k - 1] == '\\':
                            k += 1
                            continue
                        break
                    k += 1
                out.append(blank(text[i:k]))
                i = k
                continue
            at_line_start = False
        if c == '\n':
            out.append('\n')
            i += 1
            at_line_start = True
        elif text.startswith('//', i):
            k = text.find('\n', i)
            if k < 0:
                k = n
            out.append(' ' * (k - i))
            i = k
        elif text.startswith('/*', i):
            k = text.find('*/', i + 2)
            k = n if k < 0 else k + 2
            out.append(blank(text[i:k]))
            i = k
        elif c == '"' or c == "'":
            # A literal never spans a line: an unterminated one (an apostrophe in prose
            # inside a macro argument, say) ends at the newline, which stays.
            k = i + 1
            while k < n and text[k] != c and text[k] != '\n':
                if text[k] == '\\' and k + 1 < n and text[k + 1] != '\n':
                    k += 1
                k += 1
            if k < n and text[k] == c:
                k += 1
                out.append(c + ' ' * (k - i - 2) + c)
            else:
                out.append(c + ' ' * (k - i - 1))
            i = k
        else:
            out.append(c)
            i += 1
    masked = ''.join(out)
    if len(masked) != n or masked.count('\n') != text.count('\n'):
        fail('internal: the masked view does not line up with the source')
    return masked


def blank(s):
    return ''.join('\n' if ch == '\n' else ' ' for ch in s)


# ----------------------------------------------------------------------------------------------
# Name extraction helpers.
# ----------------------------------------------------------------------------------------------

def top_level_groups(text, open_ch='(', close_ch=')'):
    """Spans (start, end) of the top-level open_ch...close_ch groups of text; end is inclusive."""
    groups = []
    depth = 0
    start = -1
    for i, ch in enumerate(text):
        if ch == open_ch:
            if depth == 0:
                start = i
            depth += 1
        elif ch == close_ch:
            if depth > 0:
                depth -= 1
                if depth == 0:
                    groups.append((start, i))
    return groups


def find_top_level(text, chars):
    """Index of the first character of `chars` outside (), [] and {}; -1 if none."""
    depth = 0
    for i, ch in enumerate(text):
        if ch in '([{':
            depth += 1
        elif ch in ')]}':
            depth -= 1
        elif depth == 0 and ch in chars:
            return i
    return -1


def strip_trailing_brackets(text):
    """Remove trailing [...] groups (array declarators)."""
    text = text.rstrip()
    while text.endswith(']'):
        depth = 0
        i = len(text) - 1
        while i >= 0:
            if text[i] == ']':
                depth += 1
            elif text[i] == '[':
                depth -= 1
                if depth == 0:
                    break
            i -= 1
        if i < 0:
            return None
        text = text[:i].rstrip()
    return text


def qualified_suffix(text):
    """
    The longest suffix of text that is a qualified identifier: segments `~?ident<...>?` joined by
    `::`. Returns (name, rest) or (None, text). The destructor tilde is only accepted on the last
    segment.
    """
    text = text.rstrip()
    i = len(text)
    first = True
    while True:
        j = i
        if j > 0 and text[j - 1] == '>':
            depth = 0
            k = j - 1
            while k >= 0:
                if text[k] == '>':
                    depth += 1
                elif text[k] == '<':
                    depth -= 1
                    if depth == 0:
                        break
                k -= 1
            if k < 0:
                return None, text
            j = k
            while j > 0 and text[j - 1] in ' \t':
                j -= 1
        ident_end = j
        while j > 0 and (text[j - 1].isalnum() or text[j - 1] == '_'):
            j -= 1
        if j == ident_end or text[j].isdigit():
            return None, text
        if first and j > 0 and text[j - 1] == '~':
            j -= 1
        first = False
        i = j
        if i >= 2 and text[i - 2:i] == '::':
            i -= 2
            continue
        break
    name = text[i:]
    if i > 0 and text[i - 1] in '.>':  # member access, not a definition
        return None, text
    return name, text[:i]


def last_segment(name):
    seg = name.rsplit('::', 1)[-1]
    seg = seg.split('<', 1)[0]
    return seg.lstrip('~')


def normalise_ws(text):
    return re.sub(r'\s+', ' ', text).strip()


def parse_function_signature(sig):
    """
    sig is the declaration text before its `{` or `;`, with templates and comments removed.
    Returns (name, None) or (None, reason).
    """
    groups = top_level_groups(sig)
    if not groups:
        return None, 'no parameter list'
    params = None
    for start, end in groups:
        if PARAM_TAIL_RE.match(sig[end + 1:]):
            params = (start, end)
            break
    if params is None:
        return None, 'no parameter list followed by a body'
    pretext = sig[:params[0]].rstrip()

    # Operators, first because `operator()` ends with a parenthesis of its own.
    m = None
    for m in re.finditer(r'\boperator\b', pretext):
        pass
    if m is None and pretext.endswith(')'):
        # Parenthesised name: LUALIB_API int (luaL_foo) (lua_State *L)
        inner = top_level_groups(pretext)
        if inner and inner[-1][1] == len(pretext) - 1:
            content = pretext[inner[-1][0] + 1:inner[-1][1]].strip()
            name, rest = qualified_suffix(content)
            if name is not None and rest.strip() == '':
                return name, None
        return None, 'name is not an identifier'
    if m is not None:
        tail = pretext[m.end():]
        qualifier_text = pretext[:m.start()].rstrip()
        if qualifier_text.endswith('::'):
            qualifier, _ = qualified_suffix(qualifier_text[:-2])
            if qualifier is None:
                return None, 'operator qualifier'
            qualifier += '::'
        else:
            qualifier = ''
        tail_ws = normalise_ws(tail)
        if tail_ws == '':
            return None, 'operator without a symbol'
        if re.match(r'^[A-Za-z_]', tail_ws):
            # Conversion operator or new/delete: keep single spaces, tidy `new []`.
            tail_ws = re.sub(r'\s*\[\s*\]', '[]', tail_ws)
            tail_ws = re.sub(r'\s*([*&])\s*', r'\1', tail_ws)
            return qualifier + 'operator ' + tail_ws, None
        return qualifier + 'operator' + re.sub(r'\s+', '', tail_ws), None

    name, rest = qualified_suffix(pretext)
    if name is None:
        return None, 'name is not an identifier'
    if last_segment(name) in NOT_A_NAME:
        return None, 'name is a keyword'
    if rest.rstrip().endswith('::'):
        return None, 'dangling scope operator'
    return name, None


def parse_static_declaration(decl):
    """decl is the text before the terminating `;`, `=` or `{`. Returns (name, None) or (None, reason)."""
    text = decl.rstrip()
    if text.endswith(')'):
        groups = top_level_groups(text)
        if not groups or groups[-1][1] != len(text) - 1:
            return None, 'unbalanced parentheses'
        last = groups[-1]
        pre = text[:last[0]].rstrip()
        if pre.endswith(')'):
            # Function pointer: static R (*s_name[N])(args)
            inner_groups = top_level_groups(pre)
            if not inner_groups or inner_groups[-1][1] != len(pre) - 1:
                return None, 'unbalanced parentheses'
            content = pre[inner_groups[-1][0] + 1:inner_groups[-1][1]]
            # Drop the calling convention (keyword or macro such as WINAPI), a pointer-to-member's
            # `Class::`, and the stars.
            content = re.sub(r'^\s*(?:[A-Za-z_]\w*\s*(?:::\s*)?)*\*+\s*', '', content)
            content = strip_trailing_brackets(content)
            if content is None:
                return None, 'array declarator'
            name, rest = qualified_suffix(content)
            if name is None or rest.strip() != '':
                return None, 'function pointer name'
            return name, None
        # Constructor-initialised static: static T s_name(args)
        text = pre
    text = strip_trailing_brackets(text)
    if text is None:
        return None, 'array declarator'
    name, rest = qualified_suffix(text)
    if name is None:
        return None, 'name is not an identifier'
    if last_segment(name) in NOT_A_NAME:
        return None, 'name is a keyword'
    if rest.strip() == '' and '::' not in name:
        return None, 'no type before the name'
    return name, None


# ----------------------------------------------------------------------------------------------
# Scope tracking: which namespaces, classes and functions enclose a given position.
# ----------------------------------------------------------------------------------------------

NAMESPACE_HEAD_RE = re.compile(r'^(?:inline\s+)?namespace(?:\s+([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*))?$')
CLASS_HEAD_RE = re.compile(
    r'^(?:template\s*<.*>\s*)?(?:class|struct|union)\s+'
    r'(?:__declspec\s*\([^)]*\)\s+|[A-Z_][A-Z0-9_]*\s+)*'
    r'([A-Za-z_]\w*(?:<[^{}]*>)?)\s*(?:final\s*)?(?::(?!:)[^{}]*)?$', re.S)
ANON_AGGREGATE_RE = re.compile(r'^(?:typedef\s+)?(?:class|struct|union)\s*$')
EXTERN_C_RE = re.compile(r'^extern\s*""\s*$')


class Scope:
    __slots__ = ('kind', 'name')

    def __init__(self, kind, name):
        self.kind = kind    # 'namespace' | 'class' | 'function' | 'other'
        self.name = name    # qualified name for namespace/class/function, else None


def scope_prefix(stack):
    """The qualification a declaration at this point receives from its enclosing scopes."""
    for scope in reversed(stack):
        if scope.kind == 'function':
            return scope.name
    parts = [s.name for s in stack if s.kind in ('namespace', 'class')]
    return '::'.join(parts)


def classify_head(head, stack):
    head = normalise_ws(head)
    head = ACCESS_LABEL_RE.sub('', head).strip()
    while True:
        stripped = ACCESS_LABEL_RE.sub('', head).strip()
        if stripped == head:
            break
        head = stripped
    if head == '':
        return Scope('other', None)
    m = NAMESPACE_HEAD_RE.match(head)
    if m:
        return Scope('namespace', m.group(1)) if m.group(1) else Scope('other', None)
    if EXTERN_C_RE.match(head) or ANON_AGGREGATE_RE.match(head):
        return Scope('other', None)
    if head.startswith('enum'):
        return Scope('other', None)
    m = CLASS_HEAD_RE.match(head)
    if m and find_top_level(head, '=') < 0:
        return Scope('class', m.group(1))
    first_word = re.match(r'[A-Za-z_]\w*', head)
    if first_word and first_word.group(0) in CONTROL_HEAD:
        return Scope('other', None)
    if find_top_level(head, '=') >= 0 or '(' not in head:
        return Scope('other', None)
    sig = strip_template_prefix(head)
    name, _ = parse_function_signature(sig)
    if name is None:
        return Scope('other', None)
    prefix = scope_prefix(stack)
    if prefix:
        name = prefix + '::' + name
    return Scope('function', name)


def strip_template_prefix(text):
    text = text.lstrip()
    while text.startswith('template'):
        rest = text[len('template'):].lstrip()
        if not rest.startswith('<'):
            break
        depth = 0
        i = 0
        while i < len(rest):
            if rest[i] == '<':
                depth += 1
            elif rest[i] == '>':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        text = rest[i + 1:].lstrip()
    return text


# ----------------------------------------------------------------------------------------------
# Tags.
# ----------------------------------------------------------------------------------------------

class Tag:
    __slots__ = ('kind', 'text', 'file', 'line', 'raw', 'verify', 'scope')

    def __init__(self, kind, text, file, line, raw):
        self.kind = kind
        self.text = text
        self.file = file
        self.line = line        # 1-based
        self.raw = raw
        self.verify = None
        self.scope = ''


def split_top_level_commas(text):
    items = []
    depth = 0
    cur = []
    for ch in text:
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        if ch == ',' and depth == 0:
            items.append(''.join(cur))
            cur = []
        else:
            cur.append(ch)
    items.append(''.join(cur))
    return items


def parse_addresses(text):
    """[(address, template_args)] or None when the address text is not understood."""
    text = text.strip()
    m = re.match(r'^(0x[0-9A-Fa-f]{1,8})\s*(?:<(.*)>)?$', text, re.S)
    if m:
        return [(int(m.group(1), 16), normalise_ws(m.group(2) or ''))]
    if text.startswith('[') and text.endswith(']'):
        result = []
        for item in split_top_level_commas(text[1:-1]):
            item = item.strip()
            m = re.match(r'^(0x[0-9A-Fa-f]{1,8})\s*(?:<(.*)>)?$', item, re.S)
            if not m:
                return None
            result.append((int(m.group(1), 16), normalise_ws(m.group(2) or '')))
        return result if result else None
    return None


def collect_declaration(lines, masked_lines, start, terminators, max_lines=400):
    """
    Join the masked lines from `start` until the first top-level character of `terminators`
    (`{` or `;` for a function, plus `=` for a static so an aggregate initialiser is not entered).
    Returns (text_before_terminator, terminator, line_index_of_first_code) or (None, reason, _).
    """
    text = []
    depth = 0
    first = None
    i = start
    while i < len(lines) and i < start + max_lines:
        line = masked_lines[i]
        if first is None and line.strip() != '':
            first = i
        for ch in line:
            if ch in '([':
                depth += 1
            elif ch in ')]':
                depth -= 1
            elif depth == 0 and ch in terminators:
                return ''.join(text), ch, first
            text.append(ch)
        text.append('\n')
        i += 1
    if first is None:
        return None, 'nothing follows the tag', first
    return None, 'no body or terminator within %d lines' % max_lines, first


def c_string(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


# ----------------------------------------------------------------------------------------------
# Main pass.
# ----------------------------------------------------------------------------------------------

class Row:
    __slots__ = ('name', 'address', 'file', 'line', 'targs', 'verify', 'kind')

    def __init__(self, kind, name, address, file, line, targs, verify):
        self.kind = kind
        self.name = name
        self.address = address
        self.file = file
        self.line = line
        self.targs = targs
        self.verify = verify


class Unparsed:
    __slots__ = ('tag', 'reason', 'source')

    def __init__(self, tag, reason, source):
        self.tag = tag
        self.reason = reason
        self.source = source


def scan_file(root, rel, results):
    path = os.path.join(root, rel)
    with open(path, 'rb') as f:
        data = f.read()
    text = data.decode('utf-8-sig', errors='replace').replace('\r\n', '\n').replace('\r', '\n')
    lines = text.split('\n')
    tags = []
    for idx, line in enumerate(lines):
        m = TAG_RE.match(line)
        if m:
            tags.append(Tag(m.group(1), m.group(2), rel, idx + 1, line.rstrip()))
    if not tags:
        return
    masked = mask_source(text)
    masked_lines = masked.split('\n')

    # Line start offsets, to map tag lines to positions in the masked text.
    line_start = [0]
    for line in lines[:-1]:
        line_start.append(line_start[-1] + len(line) + 1)

    # Sweep the masked text once, snapshotting the scope stack at each tag line.
    stack = []
    head_start = 0
    paren_depth = 0
    tag_i = 0
    tag_positions = [line_start[t.line - 1] for t in tags]
    i = 0
    n = len(masked)
    while i < n:
        while tag_i < len(tags) and tag_positions[tag_i] <= i:
            tags[tag_i].scope = scope_prefix(stack)
            tag_i += 1
        ch = masked[i]
        if ch == '(':
            paren_depth += 1
        elif ch == ')':
            paren_depth = max(0, paren_depth - 1)
        elif ch == '{':
            stack.append(classify_head(masked[head_start:i], stack))
            head_start = i + 1
            paren_depth = 0
        elif ch == '}':
            if stack:
                stack.pop()
            head_start = i + 1
            paren_depth = 0
        elif ch == ';' and paren_depth == 0:
            head_start = i + 1
        i += 1
    while tag_i < len(tags):
        tags[tag_i].scope = scope_prefix(stack)
        tag_i += 1

    # VERIFY attaches to the OFFSET tag on the very next line.
    by_line = {t.line: t for t in tags}
    for t in tags:
        if t.kind == 'OFFSET':
            prev = by_line.get(t.line - 1)
            if prev is not None and prev.kind == 'VERIFY':
                m = VERIFY_RE.match(prev.text)
                t.verify = m.group(2) if m else 'unknown'
                results['verify_attached'].add((prev.file, prev.line))

    for t in tags:
        results['tags'].append(t)
        if t.kind not in ('OFFSET', 'STATIC'):
            continue
        addresses = parse_addresses(t.text)
        if addresses is None:
            results['unparsed'].append(Unparsed(t, 'address text not understood', t.raw))
            continue
        decl, term, first = collect_declaration(
            lines, masked_lines, t.line, '{;' if t.kind == 'OFFSET' else '{;=')
        source_line = lines[first].rstrip() if first is not None else ''
        if decl is None:
            results['unparsed'].append(Unparsed(t, term, source_line))
            continue
        stripped = strip_template_prefix(decl)
        # The row points at the line that carries the name: past blank lines and `template <...>`.
        first = t.line + decl[:len(decl) - len(stripped)].count('\n')
        decl = stripped
        if t.kind == 'OFFSET':
            name, reason = parse_function_signature(decl)
        else:
            name, reason = parse_static_declaration(decl)
        if name is None:
            results['unparsed'].append(Unparsed(t, reason, source_line))
            continue
        if t.scope:
            name = t.scope + '::' + name
        results['parsed'] += 1
        for address, targs in addresses:
            results['rows'].append(Row(t.kind, name, address, rel, first + 1, targs, t.verify))


def walk(root):
    files = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIRS)
        for fn in sorted(filenames):
            if fn.lower().endswith(SOURCE_EXTENSIONS):
                rel = os.path.relpath(os.path.join(dirpath, fn), root).replace('\\', '/')
                files.append(rel)
    files.sort()
    return files


def is_header(rel):
    return rel.lower().endswith(HEADER_EXTENSIONS)


def tree_of(rel):
    parts = rel.split('/')
    return '/'.join(parts[:2]) if len(parts) > 2 else parts[0]


DEF_HEADER = """\
// {title}
//
// GENERATED by core/tools/offsets/extract_offsets.py from the client fork's {tag} tags.
// Do not edit: re-run the tool after pulling upstream and review the diff.
//
// One row per address, as an X-macro: {macro}{columns}.
// Every text column is a C string literal; the address is Wow.exe (build 12340) virtual address.
// Rows are sorted by address, then name, then file and line, so the file diffs cleanly.
//
// This table is data, not an interface. Include it from ONE translation unit only, with
// {macro} defined to append to a lookup table (name -> address, address -> name). Never
// include it from a header: it is {count} rows.
//
{notes}// Copyright (C) 2026 WarcraftXL
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#ifndef {macro}
#error "define {macro}{columns} before including {filename}"
#endif

"""

FUNCTION_NOTES = """\
// Columns:
//   name     the qualified C++ name as written in the fork (enclosing namespaces and classes
//            applied; explicit specialisations keep their <args>; operators as `operator+=`,
//            `operator()`, `operator uint32_t`).
//   address  one instantiation's address. A template tag lists several addresses: one row each.
//   verify   "manual", "auto" or "none": the VERIFY: tag above the OFFSET: tag, i.e. whether the
//            decompiled body was checked by hand against the original. A confidence signal.
//   targs    the instantiation's template arguments when the tag gives them ("" otherwise);
//            instantiations tagged without arguments differ by address only.
//   file     the source file, relative to the client checkout, forward slashes.
//   line     the 1-based line of the definition (the line after the tag block).
//
"""

STATIC_NOTES = """\
// Columns:
//   name     the qualified C++ name (enclosing namespaces and classes applied). A function-local
//            static is qualified by its function: `CmdLineGetString::buffer`.
//   address  the static's address.
//   file     the source file, relative to the client checkout, forward slashes.
//   line     the 1-based line of the declaration (the line after the tag).
//
"""


def row_key(r):
    return (r.address, r.name, r.file, r.line, r.targs)


def write_functions(path, rows):
    rows = sorted(rows, key=row_key)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(DEF_HEADER.format(
            title='Function addresses of Wow.exe 3.3.5a (12340), derived from the client fork.',
            tag='OFFSET:', macro='WXL_OFFSET_FUNCTION',
            columns='(name, address, verify, targs, file, line)',
            count=len(rows), notes=FUNCTION_NOTES, filename='functions.def'))
        for r in rows:
            f.write('WXL_OFFSET_FUNCTION(%s, 0x%08X, %s, %s, %s, %d)\n' % (
                c_string(r.name), r.address, c_string(r.verify or 'none'),
                c_string(r.targs), c_string(r.file), r.line))


def write_statics(path, rows):
    rows = sorted(rows, key=row_key)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(DEF_HEADER.format(
            title='Static and global addresses of Wow.exe 3.3.5a (12340), derived from the client fork.',
            tag='STATIC:', macro='WXL_OFFSET_STATIC',
            columns='(name, address, file, line)',
            count=len(rows), notes=STATIC_NOTES, filename='statics.def'))
        for r in rows:
            f.write('WXL_OFFSET_STATIC(%s, 0x%08X, %s, %d)\n' % (
                c_string(r.name), r.address, c_string(r.file), r.line))


def write_unparsed(path, unparsed):
    unparsed = sorted(unparsed, key=lambda u: (u.tag.file, u.tag.line))
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# OFFSET:/STATIC: tags the tool could not confidently name: %d\n' % len(unparsed))
        f.write('# Each entry: file:line  KIND  reason, then the tag line and the first source line after it.\n')
        f.write('# These addresses are NOT in functions.def / statics.def. Fix the parser or the source.\n\n')
        for u in unparsed:
            f.write('%s:%d  %s  %s\n' % (u.tag.file, u.tag.line, u.tag.kind, u.reason))
            f.write('    %s\n' % u.tag.raw.strip())
            if u.source.strip():
                f.write('    %s\n' % u.source.strip())
            f.write('\n')


def pct(part, whole):
    return 100.0 * part / whole if whole else 0.0


def write_summary(path, root, files, results, out_dir):
    tags = results['tags']
    rows = results['rows']
    unparsed = results['unparsed']
    by_kind = collections.Counter(t.kind for t in tags)
    offset_tags = [t for t in tags if t.kind == 'OFFSET']
    static_tags = [t for t in tags if t.kind == 'STATIC']
    fn_rows = [r for r in rows if r.kind == 'OFFSET']
    st_rows = [r for r in rows if r.kind == 'STATIC']
    un_offset = [u for u in unparsed if u.tag.kind == 'OFFSET']
    un_static = [u for u in unparsed if u.tag.kind == 'STATIC']
    lines = []
    w = lines.append

    w('Offset extraction summary')
    w('=========================')
    # The folder name only: the file is committed and must not carry a machine's absolute path.
    w('client checkout : %s' % os.path.basename(root.rstrip('/\\')))
    w('source files    : %d scanned, %d carry tags' % (len(files), len({t.file for t in tags})))
    w('')
    w('Tags')
    w('----')
    for kind in ('OFFSET', 'STATIC', 'VERIFY', 'INLINED', 'NOTE'):
        w('  %-8s %6d' % (kind + ':', by_kind.get(kind, 0)))
    w('')
    w('Per tree (top two path components)')
    trees = collections.defaultdict(collections.Counter)
    for t in tags:
        trees[tree_of(t.file)][t.kind] += 1
    for tree in sorted(trees):
        c = trees[tree]
        w('  %-22s OFFSET %6d  STATIC %5d  VERIFY %6d  INLINED %3d  NOTE %3d' % (
            tree, c['OFFSET'], c['STATIC'], c['VERIFY'], c['INLINED'], c['NOTE']))
    w('')
    w('Headers versus sources')
    for kind, tl in (('OFFSET', offset_tags), ('STATIC', static_tags)):
        h = sum(1 for t in tl if is_header(t.file))
        w('  %-8s headers %5d   sources %6d' % (kind + ':', h, len(tl) - h))
    w('')
    w('Reconciliation (the tool fails if these do not add up)')
    w('------------------------------------------------------')
    off_parsed = len(offset_tags) - len(un_offset)
    st_parsed = len(static_tags) - len(un_static)
    w('  OFFSET tags %6d = parsed %6d + unparsed %4d   -> functions.def rows %6d  coverage %6.2f%%' % (
        len(offset_tags), off_parsed, len(un_offset), len(fn_rows), pct(off_parsed, len(offset_tags))))
    w('  STATIC tags %6d = parsed %6d + unparsed %4d   -> statics.def rows   %6d  coverage %6.2f%%' % (
        len(static_tags), st_parsed, len(un_static), len(st_rows), pct(st_parsed, len(static_tags))))
    w('  (a template tag with N addresses yields N rows; the parsed/unparsed split counts tags)')
    w('')
    w('Template tags (bracketed OFFSET:), by number of addresses')
    hist = collections.Counter()
    targ_tags = 0
    for t in offset_tags:
        a = parse_addresses(t.text)
        if a is not None and t.text.strip().startswith('['):
            hist[len(a)] += 1
            if any(ta for _, ta in a):
                targ_tags += 1
    w('  tags %d, with template arguments in the tag %d' % (sum(hist.values()), targ_tags))
    w('  ' + ', '.join('%d addresses: %d' % (k, hist[k]) for k in sorted(hist)))
    w('')
    w('VERIFY (confidence that the decompiled body matches the original)')
    w('-----------------------------------------------------------------')
    vc = collections.Counter((t.verify or 'none') for t in offset_tags)
    for k in ('manual', 'auto', 'none', 'unknown'):
        if vc.get(k):
            w('  OFFSET tags with VERIFY %-8s %6d' % (k + ':', vc[k]))
    orphans = [t for t in tags if t.kind == 'VERIFY' and (t.file, t.line) not in results['verify_attached']]
    w('  VERIFY tags not directly above an OFFSET tag: %d' % len(orphans))
    for t in orphans:
        w('    %s:%d  %s' % (t.file, t.line, t.raw.strip()))
    w('')
    w('Unparsed residue, by reason')
    w('---------------------------')
    rc = collections.Counter((u.tag.kind, u.reason) for u in unparsed)
    for (kind, reason), n in sorted(rc.items(), key=lambda kv: (-kv[1], kv[0])):
        w('  %5d  %-7s %s' % (n, kind, reason))
    w('  (see unparsed.txt)')
    w('')

    w('Addresses carrying more than one row (thunks, Win32/Win64 twins, shared bodies)')
    w('------------------------------------------------------------------------------')
    for label, rl in (('functions', fn_rows), ('statics', st_rows)):
        by_addr = collections.defaultdict(list)
        for r in rl:
            by_addr[r.address].append(r)
        dup = {a: v for a, v in by_addr.items() if len(v) > 1}
        same_name = sum(1 for v in dup.values() if len({r.name for r in v}) == 1)
        w('  %s: %d addresses with several rows (%d of them one name tagged in several files, %d with different names)' % (
            label, len(dup), same_name, len(dup) - same_name))
        for a in sorted(dup):
            names = sorted({r.name + (('<' + r.targs + '>') if r.targs else '') for r in dup[a]})
            locs = sorted({r.file for r in dup[a]})
            w('    0x%08X  %s' % (a, ' | '.join(names)))
            if len(names) == 1:
                w('              in %s' % ', '.join(locs))
    w('')

    w('Names carrying more than one address (overloads, template instantiations, same name in two scopes)')
    w('--------------------------------------------------------------------------------------------------')
    for label, rl in (('functions', fn_rows), ('statics', st_rows)):
        by_name = collections.defaultdict(set)
        for r in rl:
            by_name[r.name].add(r.address)
        multi = {nm: v for nm, v in by_name.items() if len(v) > 1}
        w('  %s: %d names with several addresses' % (label, len(multi)))
        for nm in sorted(multi):
            w('    %-60s %s' % (nm, ' '.join('0x%08X' % a for a in sorted(multi[nm]))))
    w('')

    inl = [t for t in tags if t.kind == 'INLINED']
    w('INLINED tags (no address: the body is inlined in the original; ranges are call-site spans)')
    w('------------------------------------------------------------------------------------------')
    for t in inl:
        w('  %s:%d  %s' % (t.file, t.line, t.text))
    w('')
    notes = [t for t in tags if t.kind == 'NOTE']
    w('NOTE: lines (ordinary comments in third-party code, not address tags)')
    for t in notes:
        w('  %s:%d  %s' % (t.file, t.line, t.text))
    w('')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    return lines


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('client', help='path to the client checkout (read-only input)')
    ap.add_argument('--out', default=None,
                    help='output directory (default: ../../offsets relative to this script)')
    ap.add_argument('--quiet', action='store_true', help='do not print the summary')
    args = ap.parse_args()

    root = os.path.abspath(args.client)
    if not os.path.isdir(root):
        fail('not a directory: %s' % root)
    out_dir = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'offsets')
    out_dir = os.path.abspath(out_dir)

    results = {
        'tags': [], 'rows': [], 'unparsed': [], 'parsed': 0, 'verify_attached': set(),
    }
    files = walk(root)
    for rel in files:
        scan_file(root, rel, results)

    # Reconcile before writing anything.
    tags = results['tags']
    for kind in ('OFFSET', 'STATIC'):
        total = sum(1 for t in tags if t.kind == kind)
        unparsed = sum(1 for u in results['unparsed'] if u.tag.kind == kind)
        parsed_addresses = 0
        for t in tags:
            if t.kind == kind and not any(u.tag is t for u in results['unparsed']):
                parsed_addresses += len(parse_addresses(t.text))
        rows = sum(1 for r in results['rows'] if r.kind == kind)
        if parsed_addresses != rows:
            fail('%s: %d addresses in parsed tags but %d rows' % (kind, parsed_addresses, rows))
        parsed_tags = total - unparsed
        if parsed_tags < 0:
            fail('%s: more unparsed tags than tags' % kind)
    if results['parsed'] + len(results['unparsed']) != sum(1 for t in tags if t.kind in ('OFFSET', 'STATIC')):
        fail('parsed + unparsed tags != OFFSET + STATIC tags')
    # Every row must point at a real tag of its own kind, in a file we read.
    seen = {(t.file, t.kind) for t in tags}
    for r in results['rows']:
        if (r.file, r.kind) not in seen:
            fail('row without a tag: %s %s' % (r.file, r.name))

    os.makedirs(out_dir, exist_ok=True)
    write_functions(os.path.join(out_dir, 'functions.def'), [r for r in results['rows'] if r.kind == 'OFFSET'])
    write_statics(os.path.join(out_dir, 'statics.def'), [r for r in results['rows'] if r.kind == 'STATIC'])
    write_unparsed(os.path.join(out_dir, 'unparsed.txt'), results['unparsed'])
    lines = write_summary(os.path.join(out_dir, 'summary.txt'), root, files, results, out_dir)
    if not args.quiet:
        stop = next((i for i, l in enumerate(lines) if l.startswith('Addresses carrying')), len(lines))
        print('\n'.join(lines[:stop]))
        print('written to %s' % out_dir)


if __name__ == '__main__':
    main()
