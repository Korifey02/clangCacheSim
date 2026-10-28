#!/bin/sh
# Использование: run_program_test.sh <каталог с ccs-clang> <файл.c>
# Ожидания берутся из строк "// EXPECT: уровень переменная метрика значение",
# конфигурация кэша — из "// CCS_CACHE: ...".
set -u
bin=$1
src=$2

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

cache=$(sed -n 's|^// CCS_CACHE: *||p' "$src")
case "$src" in
  *.c) cc="$bin/ccs-clang" ;;
  *) cc="$bin/ccs-clang++" ;;
esac
"$cc" -O2 "$src" -o "$tmp/prog" || exit 1
CCS_FORMAT=tsv CCS_REPORT="$tmp/report.tsv" CCS_CACHE="$cache" "$tmp/prog" >/dev/null

sed -n 's|^// EXPECT: *||p' "$src" >"$tmp/expect"
if [ ! -s "$tmp/expect" ]; then
  echo "$src: нет строк EXPECT" >&2
  exit 1
fi

failed=0
while read -r level var key want; do
  got=$(awk -F'\t' -v l="$level" -v v="$var" -v k="$key" \
    '$1 == l && $2 == v && $3 == k { print $4 }' "$tmp/report.tsv")
  [ -n "$got" ] || got=0
  if [ "$got" != "$want" ]; then
    echo "FAIL $level $var $key: ожидалось $want, получено $got"
    failed=1
  fi
done <"$tmp/expect"

if [ "$failed" -ne 0 ]; then
  echo "--- отчёт ---"
  cat "$tmp/report.tsv"
fi
exit "$failed"
