// L1 32 КиБ / (64 Б * 8) = 64 набора, адреса с шагом 4096 попадают в один набор.
// 8 таких строк по кругу 10 раз: набор 8-канальный, всё помещается,
// промахов только 8 холодных.
// EXPECT: L1 - accesses 80
// EXPECT: L1 - misses 8
// EXPECT: L1 - hits 72
// EXPECT: L2 - accesses 8
// EXPECT: L2 - misses 8
// EXPECT: mem - reads 8

char buf[8 * 4096] __attribute__((aligned(4096)));

int main(void) {
  int s = 0;
  for (int r = 0; r < 10; r++)
    for (int k = 0; k < 8; k++)
      s += buf[k * 4096];
  return s;
}
