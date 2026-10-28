#include <cstdio>
#include <vector>

int c[10000];

int main() {
  const int n = 100;
  std::vector<int> a(10000), b(10000);
  for (int i = 0; i < 10000; i++) { a[i] = i % 6; b[i] = i % 11; }
  for (int i = 0; i < n; i++)
    for (int j = 0; j < n; j++) {
      c[i * n + j] = 0;
      for (int k = 0; k < n; k++)
        c[i * n + j] = c[i * n + j] + a[i * n + k] * b[k * n + j];
    }
  std::printf("c[42]=%d\n", c[42]);
}
