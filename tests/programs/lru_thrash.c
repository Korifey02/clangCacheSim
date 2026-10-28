// То же, но 9 строк в 8-канальный набор L1. При LRU и обходе по кругу
// каждое обращение вытесняет строку, которая понадобится следующей: 90 из 90 — промахи.
// В L2 (512 наборов) строки k*64 для k = 0..8 занимают наборы 0, 64, ..., 448, 0:
// в наборе 0 оказываются две строки, места хватает, поэтому промахов в L2 только 9 холодных.
// EXPECT: L1 - accesses 90
// EXPECT: L1 - misses 90
// EXPECT: L1 - hits 0
// EXPECT: L2 - accesses 90
// EXPECT: L2 - misses 9
// EXPECT: L2 - hits 81
// EXPECT: mem - reads 9

char buf[9 * 4096] __attribute__((aligned(4096)));

int main(void) {
  int s = 0;
  for (int r = 0; r < 10; r++)
    for (int k = 0; k < 9; k++)
      s += buf[k * 4096];
  return s;
}
