#!/usr/bin/env bash
# run.sh - builds the PC harness (Maki's indexing and searching, on Linux
# files) and runs the search checks on the test files in DIR (default:
# tests/files, made by mkfiles.py).
set -e
T=$(cd "$(dirname "$0")" && pwd)
D=${1:-$T/files}
[ -f "$D/big.txt" ] || python3 "$T/mkfiles.py" "$D"
gcc -DMAKI_PC -O1 -g -fsanitize=address,undefined -Wall -Wno-pointer-sign -Wno-unused \
    -Wno-pointer-to-int-cast -I"$T/fake" -o /tmp/maki-harness "$T/harness.c" "$T/../src/src.c"
check() {   # FILE PATTERN EXPECTED-MATCH-LINE (0: none) [back]
  out=$(timeout 10 /tmp/maki-harness "$D/$1" "$2" $4) || { echo "FAIL $1 '$2': hung or crashed"; bad=1; return; }
  case "$out" in *"match line $3,"*) echo "ok   $1 '$2' -> $3";; *) echo "FAIL $1 '$2': $out (wanted $3)"; bad=1;; esac
}
bad=0
check ansi.txt green 1
check ansi.txt Red 1
check ansi.txt under 6
check ansi.txt underline 0
check ansi.txt '^tab' 5
check ansi.txt 'orange$' 8
check ansi.txt '\d+ blue' 8
check ansi.txt bold 1
check ansi.txt 'still' 2
check big.txt needle 50000
check big.txt 'Line\s+199999' 199999
check big.txt '1999[0-9]9 of' 199909
check big.txt nomatch 0
check utf8.txt quotes 1
check art.nfo box 2
check ansi.txt yellow 0 back
check big.txt needle 0 back
exit $bad
