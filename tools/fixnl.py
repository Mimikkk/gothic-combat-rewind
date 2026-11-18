"""Repair string literals that got a real line break instead of the two characters backslash-n.
usage: python fixnl.py file.cpp [...]"""
import sys


def quotes(line):
    n, esc, inchar = 0, False, False
    for i, ch in enumerate(line):
        if esc:
            esc = False
            continue
        if ch == '\\':
            esc = True
        elif ch == "'" and not n % 2:
            inchar = not inchar
        elif ch == '"' and not inchar:
            n += 1
    return n


for path in sys.argv[1:]:
    raw = open(path, 'rb').read().decode('utf-8')
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.replace('\r\n', '\n').split('\n')
    out, i, fixed = [], 0, 0
    while i < len(lines):
        line = lines[i]
        while quotes(line) % 2 == 1 and i + 1 < len(lines):
            i += 1
            line = line + '\\n' + lines[i]
            fixed += 1
        out.append(line)
        i += 1
    # char literals split the same way: c != '<LF>'
    text = nl.join(out)
    open(path, 'wb').write(text.encode('utf-8'))
    print(path, 'joined', fixed)
