#!/usr/bin/env python3
"""mkfiles.py DIR - the test files: ANSI colours and Latin-1, CP437 art,
UTF-8, and a 10 MB file of 200000 lines with 'needle' every 50000."""
import os, sys
D = sys.argv[1]
os.makedirs(D, exist_ok=True)
s = b"\x1b[1;31mRed bold\x1b[0m normal \x1b[32mgreen\nstill green on the next line\x1b[0m\n"
s += "Svenska: \xe5\xe4\xf6 \xc5\xc4\xd6\n".encode('latin-1')
s += b"\x9b33mAmiga CSI yellow\x9b0m\n"
s += b"tab\there\tand\there\n"
s += b"b\bbo\bol\bld and _\bu_\bn_\bd_\be_\br\n"
s += (b"long line " * 30) + b"END\n"
s += b"\x1b[48;5;21m256 blue bg\x1b[0m \x1b[38;2;255;128;0mtruecolor orange\x1b[0m\n"
open(os.path.join(D, 'ansi.txt'), 'wb').write(s)
art = (bytes([0xC9] + [0xCD] * 20 + [0xBB]) + b"\r\n" + bytes([0xBA]) +
       b"\x1b[1;36m  CP437 box test    \x1b[0m" + bytes([0xBA]) + b"\r\n" +
       bytes([0xC8] + [0xCD] * 20 + [0xBC]) + b"\r\n" + bytes([0xB0, 0xB1, 0xB2, 0xDB]) * 5 + b"\r\n")
open(os.path.join(D, 'art.nfo'), 'wb').write(art)
open(os.path.join(D, 'utf8.txt'), 'w', encoding='utf-8').write("UTF-8: åäö ÅÄÖ — “quotes” ─│┌┐ ✓\n")
with open(os.path.join(D, 'big.txt'), 'w') as f:
    for i in range(1, 200001):
        f.write(f"Line {i:6d} of the big file, some text to search: {'needle' if i % 50000 == 0 else 'hay'}\n")
