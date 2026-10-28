// first и second вызываются по очереди, их массивы x и y занимают одно место
// стека. Обращения через указатель в touch должны попасть к живому массиву.
// EXPECT: L1 first::x writes 16
// EXPECT: L1 second::y writes 16
// EXPECT: L1 <unknown> accesses 0

__attribute__((noinline)) void touch(int *p) {
  for (int i = 0; i < 16; i++)
    p[i * 16] = 1;
}

__attribute__((noinline)) void first(void) {
  int x[256];
  touch(x);
}

__attribute__((noinline)) void second(void) {
  int y[256];
  touch(y);
}

int main(void) {
  first();
  second();
  return 0;
}
