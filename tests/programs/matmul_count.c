// Число обращений в режиме early совпадает с тем, что записано в исходнике:
// инициализация a и b: 2 * 256 записей = 512,
// обнуление c: 16 * 16 = 256,
// внутренний цикл: 16^3 итераций * 4 обращения (чтение c, a, b, запись c) = 16384.
// Итого 17152.
// EXPECT: L1 - accesses 17152
// EXPECT: L1 c accesses 8448
// EXPECT: L1 main::a accesses 4352
// EXPECT: L1 main::b accesses 4352

int c[256];

int main(void) {
  int a[256], b[256];
  int n = 16;
  for (int i = 0; i < 256; i++) {
    a[i] = i % 6;
    b[i] = i % 11;
  }
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      c[i * n + j] = 0;
      for (int k = 0; k < n; k++)
        c[i * n + j] = c[i * n + j] + a[i * n + k] * b[k * n + j];
    }
  return 0;
}
