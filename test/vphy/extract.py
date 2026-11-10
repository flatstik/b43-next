#!/usr/bin/env python3
#
#   Copyright (C) 2026  b43-tools contributors
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License version 2
#   as published by the Free Software Foundation.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
"""Print the #include lines and the named functions of a kernel C file, in
the file's order, as a file that compiles on its own; a static function
loses its static, so that the harness can call it. #line directives keep the
lines of the original file.

A function starts on a line at column 0 that has its name before a
parenthesis and ends at the first line that is a closing brace alone, as the
kernel's style has it. A name that is not found is an error.

    extract.py FILE NAME...
"""

import re
import sys


def functions(lines, names, path):
    wanted = re.compile(r"^(?=\w)(?:[^;(]*?[\s*])?(" + "|".join(map(re.escape, names)) + r")\(")
    found, out, inside = set(), [], False
    for n, line in enumerate(lines, 1):
        if not inside and line.startswith("#include"):
            out.append(line)
            continue
        m = None if inside else wanted.match(line)
        if m:
            out.append(f'#line {n} "{path}"\n')
            inside = True
            found.add(m.group(1))
            line = re.sub(r"^static\s+", "", line)
        if inside:
            out.append(line)
            if line.rstrip() == "}":
                inside = False
                out.append("\n")
    return out, set(names) - found


def main():
    path, names = sys.argv[1], sys.argv[2:]
    out, missing = functions(open(path).readlines(), names, path)
    if missing:
        sys.exit(f"extract.py: not in {path}: {' '.join(sorted(missing))}")
    sys.stdout.writelines(out)


if __name__ == "__main__":
    main()
