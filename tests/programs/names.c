// Имена: глобальная переменная — по символу, локальная — функция::имя.
// Счётчики циклов в регистрах (режим early), в трассу не попадают.
// fill::buf: 256 записей + 256 чтений, g: 256 записей.
// EXPECT: L1 - accesses 768
// EXPECT: L1 fill::buf accesses 512
// EXPECT: L1 fill::buf write_hits 240
// EXPECT: L1 g accesses 256
// EXPECT: L1 g write_misses 16

int g[256] __attribute__((aligned(64)));

__attribute__((noinline)) static void fill(void) {
  int buf[256] __attribute__((aligned(64)));
  for (int i = 0; i < 256; i++)
    buf[i] = i * 3;
  for (int i = 0; i < 256; i++)
    g[i] = buf[i];
}

int main(void) {
  fill();
  return 0;
}
