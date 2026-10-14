#!/usr/bin/env python3
"""Summarise how a Pintos tree differs from a stock Pintos tree.

Usage: diff_vs_stock.py STOCK_SRC STUDENT_SRC [--out FILE.md]

Both arguments are Pintos `src/` directories. The report lists, per file,
whether it was added, removed or modified, the lines added/removed, and for
C files the functions that were added, removed or changed.

Line endings and trailing whitespace are normalised first, so a CRLF checkout
or editor whitespace does not count as a change. Build output is skipped.
"""
import argparse
import difflib
import os
import re
import sys

SKIP_DIRS = {"build", ".git"}
SKIP_SUFFIXES = (".o", ".d", ".bin", ".dsk", ".a")
TEXT_SUFFIXES = (".c", ".h", ".S", ".s", ".pl", ".pm", ".ck", ".lds", ".txt",
                 ".texi", ".tmpl", ".sh", ".py")


def walk(root):
    files = set()
    for d, dirs, names in os.walk(root):
        dirs[:] = sorted(x for x in dirs if x not in SKIP_DIRS)
        for n in names:
            if n.endswith(SKIP_SUFFIXES):
                continue
            files.add(os.path.relpath(os.path.join(d, n), root))
    return files


def read_lines(path):
    with open(path, "rb") as f:
        data = f.read()
    if b"\0" in data[:4096]:
        return None  # binary
    text = data.decode("utf-8", errors="replace")
    return [line.rstrip() for line in text.splitlines()]


IDENT = re.compile(r"([A-Za-z_]\w*)\s*\(")


def c_functions(lines):
    """Map function name -> body text, for GNU-style C (brace in column 0)."""
    funcs = {}
    i = 0
    while i < len(lines):
        if lines[i] == "{":
            # Header: lines above the brace, back to a blank line or a line
            # that ends a previous declaration/definition.
            j = i - 1
            while j >= 0 and lines[j] and not lines[j].startswith(("}", "#")) \
                    and not lines[j].rstrip().endswith((";", "*/")):
                j -= 1
            header = " ".join(lines[j + 1:i])
            k = i + 1
            while k < len(lines) and lines[k] != "}" and not lines[k].startswith("};"):
                k += 1
            m = IDENT.search(header)
            if m and "=" not in header.split("(")[0]:
                funcs[m.group(1)] = "\n".join(lines[j + 1:k + 1])
            i = k
        i += 1
    return funcs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stock")
    ap.add_argument("student")
    ap.add_argument("--out")
    args = ap.parse_args()

    stock, student = walk(args.stock), walk(args.student)
    rows, total_add, total_del = [], 0, 0
    for rel in sorted(stock | student):
        a = read_lines(os.path.join(args.stock, rel)) if rel in stock else []
        b = read_lines(os.path.join(args.student, rel)) if rel in student else []
        if a is None or b is None:
            if rel in stock and rel in student:
                continue  # binary; not compared
        a, b = a or [], b or []
        if rel in stock and rel in student and a == b:
            continue
        added = removed = 0
        for line in difflib.unified_diff(a, b, lineterm="", n=0):
            if line.startswith("+") and not line.startswith("+++"):
                added += 1
            elif line.startswith("-") and not line.startswith("---"):
                removed += 1
        status = "added" if rel not in stock else "removed" if rel not in student else "modified"
        funcs = ""
        if rel.endswith(".c"):
            fa, fb = c_functions(a), c_functions(b)
            new = sorted(set(fb) - set(fa))
            gone = sorted(set(fa) - set(fb))
            changed = sorted(n for n in set(fa) & set(fb) if fa[n] != fb[n])
            parts = []
            if new:
                parts.append("new: " + ", ".join(f"`{n}`" for n in new))
            if changed:
                parts.append("changed: " + ", ".join(f"`{n}`" for n in changed))
            if gone:
                parts.append("removed: " + ", ".join(f"`{n}`" for n in gone))
            funcs = "; ".join(parts)
        rows.append((rel, status, added, removed, funcs))
        total_add += added
        total_del += removed

    out = [f"Stock: `{args.stock}`  ", f"Student: `{args.student}`", "",
           f"**{len(rows)} files differ, +{total_add} / -{total_del} lines** "
           "(CRLF and trailing whitespace ignored; build output skipped).", "",
           "| File | Status | + | - | Functions |", "|---|---|---:|---:|---|"]
    for rel, status, added, removed, funcs in rows:
        out.append(f"| `{rel}` | {status} | {added} | {removed} | {funcs} |")
    text = "\n".join(out) + "\n"
    if args.out:
        with open(args.out, "w") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == "__main__":
    main()
