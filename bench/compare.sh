#!/usr/bin/env bash
# Старый CacheSim (интерпретатор) против clangCacheSim на одних и тех же .c файлах.
# Использование: bench/compare.sh [-v] [файл.c ...]
#   -v  дополнительно напечатать полные отчёты обоих инструментов
set -uo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
CACHESIM_EXE=${CACHESIM_EXE:-/mnt/c/Users/ikori/repositories_eduction/CacheSim/CacheSim.exe}
# Нативная сборка тем же clang, что и в ccs-clang
NATIVE_CC=${NATIVE_CC:-$(sed -n 's|^ *exec "\([^"]*\)".*|\1|p' "$ROOT/build/bin/ccs-clang" | head -1)}

verbose=0
if [ "${1:-}" = "-v" ]; then
  verbose=1
  shift
fi
[ $# -ge 1 ] || set -- "$ROOT/examples/matmul.c" "$ROOT/examples/matmul_200.c"

if [ ! -x "$CACHESIM_EXE" ]; then
  echo "Не найден старый CacheSim: $CACHESIM_EXE (задайте CACHESIM_EXE=...)" >&2
  exit 1
fi

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

now() { date +%s%N; }
ms() { echo $(( ($2 - $1) / 1000000 )); }
first_num() { grep -m1 -E "$1" "$2" | grep -oE '[0-9]+' | head -1; }

row() { printf "%-16s %10s %12s %10s   %10s %10s %12s %10s   %9s %9s\n" "$@"; }

echo
row "" "old" "CacheSim" "" "" "" "clangCacheSim" "" "native" ""
row "file" "run ms" "accesses" "L1 miss" "build ms" "run ms" "accesses" "L1 miss" "run ms" "speedup"
row "----" "------" "--------" "-------" "--------" "------" "--------" "-------" "------" "-------"

for src in "$@"; do
  name=$(basename "$src")

  s=$(now)
  "$CACHESIM_EXE" "$(wslpath -w "$src")" >"$tmp/old.txt" 2>&1
  e=$(now)
  old_ms=$(ms "$s" "$e")
  old_acc=$(first_num '^Cache access:' "$tmp/old.txt")
  old_miss=$(first_num '^Cache misses:' "$tmp/old.txt")

  s=$(now)
  "$ROOT/build/bin/ccs-clang" -O2 "$src" -o "$tmp/prog" || exit 1
  e=$(now)
  build_ms=$(ms "$s" "$e")
  s=$(now)
  CCS_REPORT="$tmp/new.txt" "$tmp/prog" >/dev/null
  e=$(now)
  new_ms=$(ms "$s" "$e")
  new_acc=$(first_num '^  accesses' "$tmp/new.txt")
  new_miss=$(first_num '^  misses' "$tmp/new.txt")

  "$NATIVE_CC" -O2 "$src" -o "$tmp/native" || exit 1
  s=$(now)
  "$tmp/native" >/dev/null
  e=$(now)
  native_ms=$(ms "$s" "$e")

  speedup=$(awk -v a="$old_ms" -v b="$new_ms" 'BEGIN { printf "x%.0f", a / (b > 0 ? b : 1) }')
  row "$name" "$old_ms" "${old_acc:-?}" "${old_miss:-?}" "$build_ms" "$new_ms" "${new_acc:-?}" "${new_miss:-?}" "$native_ms" "$speedup"

  if [ "$verbose" -eq 1 ]; then
    echo; echo "===== старый CacheSim: $name"; cat "$tmp/old.txt"
    echo "===== clangCacheSim: $name"; cat "$tmp/new.txt"; echo
  fi
done
echo
echo "speedup — во сколько раз запуск под clangCacheSim быстрее старого CacheSim (без учёта сборки)"
