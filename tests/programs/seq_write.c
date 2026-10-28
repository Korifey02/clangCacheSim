// 1024 записи int подряд = 4096 байт = 64 строки по 64 байта.
// L1: первая запись в строку — промах, остальные 15 — попадания.
// L2: каждый промах L1 читает строку снизу (write-allocate), все 64 холодные.
// При выходе 64 грязные строки L1 записываются в L2, затем 64 строки L2 — в память.
// EXPECT: L1 - accesses 1024
// EXPECT: L1 - write_misses 64
// EXPECT: L1 - write_hits 960
// EXPECT: L1 - read_misses 0
// EXPECT: L1 a write_misses 64
// EXPECT: L2 - accesses 64
// EXPECT: L2 - read_misses 64
// EXPECT: L2 - writebacks_in 64
// EXPECT: mem - reads 64
// EXPECT: mem - writes 64
// EXPECT: mem - writes_flush 64

int a[1024] __attribute__((aligned(64)));

int main(void) {
  for (int i = 0; i < 1024; i++)
    a[i] = i;
  return 0;
}
