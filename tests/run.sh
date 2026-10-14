#!/usr/bin/env bash
# run.sh - Fude's renderer (src/md.c) on the PC and as the Amiga build:
#  1. the PC harness under ASAN/UBSAN on the sample and on random
#     Markdown (crashes, hangs, memory errors);
#  2. the 68020 build under vamos, its output compared with the PC's
#     (bebbo's gcc has miscompiled md.c before: docs/design.md).
# Usage: tests/run.sh [random cases, default 300]
set -e
T=$(cd "$(dirname "$0")" && pwd)
N=${1:-300}
W=$(mktemp -d)
gcc -O1 -g -fsanitize=address,undefined -Wall -Wno-pointer-sign -Wno-unused \
    -I"$T/fake" -I"$T/../src" -o "$W/pc" "$T/mdtest.c"
"$HOME/opt/amiga/bin/m68k-amigaos-gcc" -Os -m68020 -noixemul -fomit-frame-pointer \
    -fno-common -fbbb=- -w -I"$T/../src" -o "$W/am" "$T/mdtest.c"
cp "$T/files/"* "$W/"
cd "$W"
bad=0
./pc -l -p -a -w 72 sample.md > pc.txt
vamos --cpu 68020 --stack 64 -- ./am -l -p -a -w 72 sample.md > am.txt 2>/dev/null
cmp -s pc.txt am.txt && echo "ok   sample: PC and Amiga build agree" || { echo "FAIL sample differs"; bad=1; }
python3 - "$N" <<'PY' || bad=1
import random, subprocess, sys
pieces = ["# ", "## ", "####### ", "- ", "* ", "1. ", "2) ", "> ", ">> ", "    ", "\t", "```", "```c",
          "~~~", "---", "***", "===", "| a | b |", "|---|:-:|", "| x |", "[ ] ", "[x] ", "*", "**", "_",
          "__", "~", "~~", "`", "``", "[", "]", "(", ")", "![", "](", "<", ">", "&amp;", "&#", ";", "\\",
          "http://x.y/", "www.a.b", "<http://q>", "<a@b>", "<img src=\"pic.png\" alt=\"q\">", "<b>",
          "</b>", "<a href=\"#x\">", "</a>", "<!--", "-->", "<br>", "[ref]: http://r", "[^1]", "word",
          "longwordlongwordlongwordlongwordlongword", " ", "  ", "\n", "\n\n", "ä", "€", "[!NOTE]",
          "  - ", "    - "]
bad = 0
for i in range(int(sys.argv[1])):
    rng = random.Random(i)
    open("r.md", "w").write("".join(rng.choice(pieces) for _ in range(rng.randint(1, 100))))
    w = str(rng.choice([9, 20, 40, 72, 200]))
    try:
        a = subprocess.run(["./pc", "-l", "-p", "-a", "-w", w, "r.md"], capture_output=True, timeout=10)
        subprocess.run("vamos --cpu 68020 --stack 64 -- ./am -l -p -a -w %s r.md >v.txt 2>/dev/null" % w,
                       shell=True, timeout=60)
    except subprocess.TimeoutExpired:
        print("FAIL case", i, "hangs"); bad += 1; continue
    if a.returncode or b"ERROR" in a.stderr or b"runtime error" in a.stderr:
        print("FAIL case", i, "crashed on the PC"); bad += 1
    elif a.stdout != open("v.txt", "rb").read():
        print("FAIL case", i, "Amiga build differs (width", w + ")"); bad += 1
    if bad > 3: break
print("ok   %s random cases" % sys.argv[1] if not bad else "FAIL %d random cases" % bad)
sys.exit(bad > 0)
PY
rm -rf "$W"
exit $bad
