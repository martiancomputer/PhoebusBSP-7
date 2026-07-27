#!/usr/bin/env python3
"""
Collapse duplicate `config` entries inside a single Kconfig `choice` block.

The Realtek Wi-Fi Kconfigs declare the same choice value several times, once per
supported chip, so that each chip gets its own prompt string:

	config SLOT_1_RFE_TYPE_0
	depends on (SLOT_1_8822BE || SLOT_1_8822CE)
	bool "Type 0: internal PA/LNA"

	config SLOT_1_RFE_TYPE_0
	depends on (SLOT_1_8194BE || SLOT_1_8814CR)
	bool "Type 0: internal PA/LNA 4-LAYER"

Kconfig in 7.1 rejects this ("choice value must not have a prompt in another
entry"). Merge each group into one entry: first prompt wins and the `depends on`
expressions are OR-ed, so the value stays offerable in exactly the same set of
configurations.

The `select`s must NOT simply be unioned: the same RFE type means different
things per chip. Type 3 on an 8814AE is *external* PA/LNA and selects EXT_PA /
EXT_LNA, while Type 3 on our 8192FE is *internal* PA/LNA and selects nothing.
Unioning them would switch on an external-PA RF path the board does not have.
Each select is therefore re-emitted as `select X if (<that entry's depends>)`,
which reproduces the per-chip behaviour exactly.
"""
import re
import sys

CONFIG_RE = re.compile(r'^\s*config\s+(\w+)\s*$')
CHOICE_RE = re.compile(r'^\s*choice\b')
ENDCHOICE_RE = re.compile(r'^\s*endchoice\b')
DEPENDS_RE = re.compile(r'^(\s*)depends on\s+(.*)$')
SELECT_RE = re.compile(r'^\s*select\s+')


def merge_group(entries):
    """entries: list of list-of-lines, each starting with `config X`. -> merged lines"""
    if len(entries) == 1:
        return entries[0]

    out = []
    depends = []
    selects = []
    seen_select = set()

    for i, ent in enumerate(entries):
        # this entry's own guard, needed to keep its selects chip-conditional
        own = [DEPENDS_RE.match(l).group(2).strip() for l in ent if DEPENDS_RE.match(l)]
        own_expr = ' && '.join(f'({d})' for d in own) if own else None

        for ln in ent:
            m = DEPENDS_RE.match(ln)
            if m:
                expr = m.group(2).strip()
                if expr and expr not in depends:
                    depends.append(expr)
                continue
            if SELECT_RE.match(ln):
                stripped = ln.strip()
                if own_expr and ' if ' not in stripped:
                    ln = f'{ln.rstrip()} if {own_expr}'
                key = ln.strip()
                if key not in seen_select:
                    seen_select.add(key)
                    selects.append(ln)
                continue
            if i == 0:
                out.append(ln)          # config line, type+prompt, help, ...

    # splice: config line, merged depends, body, merged selects
    indent = re.match(r'^(\s*)', out[0]).group(1)
    body = out[1:]
    res = [out[0]]
    if depends:
        joined = ' || '.join(f'({d})' if not d.startswith('(') else d
                             for d in depends)
        res.append(f'{indent}depends on {joined}')
    res.extend(body)
    res.extend(selects)
    return res


def process(path):
    lines = open(path, encoding='utf-8', errors='surrogateescape').read().split('\n')
    out = []
    i = 0
    merged_total = 0

    while i < len(lines):
        if not CHOICE_RE.match(lines[i]):
            out.append(lines[i]); i += 1; continue

        # --- inside a choice block ---
        block = [lines[i]]; i += 1
        depth = 1
        while i < len(lines):
            if CHOICE_RE.match(lines[i]):
                depth += 1
            elif ENDCHOICE_RE.match(lines[i]):
                depth -= 1
                if depth == 0:
                    break
            block.append(lines[i]); i += 1
        endline = lines[i] if i < len(lines) else 'endchoice'
        i += 1

        # split the block body into a preamble + per-config entries
        preamble, entries, cur = [], [], None
        for ln in block:
            if CONFIG_RE.match(ln):
                if cur is not None:
                    entries.append(cur)
                cur = [ln]
            elif cur is None:
                preamble.append(ln)
            else:
                cur.append(ln)
        if cur is not None:
            entries.append(cur)

        # group by symbol, preserving first-appearance order
        order, groups = [], {}
        for ent in entries:
            sym = CONFIG_RE.match(ent[0]).group(1)
            if sym not in groups:
                groups[sym] = []; order.append(sym)
            groups[sym].append(ent)

        out.extend(preamble)
        for sym in order:
            if len(groups[sym]) > 1:
                merged_total += 1
            out.extend(merge_group(groups[sym]))
        out.append(endline)

    open(path, 'w', encoding='utf-8', errors='surrogateescape').write('\n'.join(out))
    return merged_total


for p in sys.argv[1:]:
    n = process(p)
    print(f'{p}: merged {n} duplicated choice value(s)')
