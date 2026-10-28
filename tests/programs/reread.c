// Два прохода чтения по 4 КиБ: массив целиком помещается в L1 (32 КиБ),
// поэтому промахи только на первом проходе — 64 холодные строки.
// EXPECT: L1 - accesses 2048
// EXPECT: L1 - read_misses 64
// EXPECT: L1 - read_hits 1984
// EXPECT: L1 - write_misses 0
// EXPECT: L1 a accesses 2048
// EXPECT: L2 - accesses 64
// EXPECT: L2 - misses 64
// EXPECT: mem - reads 64
// EXPECT: mem - writes 0

int a[1024] __attribute__((aligned(64)));

int main(void) {
  int s = 0;
  for (int r = 0; r < 2; r++)
    for (int i = 0; i < 1024; i++)
      s += a[i];
  return s;
}
