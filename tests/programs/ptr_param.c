// Внутри fill и sum видно только указатель p. Обращения должны попасть к тем
// переменным, на которые он указывает: глобальной g и локальной main::loc.
// EXPECT: L1 g writes 1024
// EXPECT: L1 g reads 1024
// EXPECT: L1 main::loc writes 512
// EXPECT: L1 main::loc reads 512
// EXPECT: L1 <unknown> accesses 0

int g[1024];

__attribute__((noinline)) void fill(int *p, int n) {
  for (int i = 0; i < n; i++)
    p[i] = i;
}

__attribute__((noinline)) long sum(const int *p, int n) {
  long s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

int main(void) {
  int loc[512];
  fill(g, 1024);
  fill(loc, 512);
  return (int)(sum(g, 1024) + sum(loc, 512));
}
