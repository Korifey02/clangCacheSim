// Данные std::vector лежат в куче и подписываются строкой объявления вектора.
// Две переменные на одной строке различаются столбцом.
// a: 1024 int = 4096 байт, b: 512 int = 2048 байт; читаем каждый элемент дважды.
// Плюс одно чтение на вектор при создании: libstdc++ обнуляет первый элемент и
// копирует его в остальные через fill_n(..., *first) (stl_uninitialized.h).
// EXPECT: L1 heap@heap_vector.cpp:14:20 reads 2049
// EXPECT: L1 heap@heap_vector.cpp:14:29 reads 1025
// EXPECT: heap heap@heap_vector.cpp:14:20 bytes 4096
// EXPECT: heap heap@heap_vector.cpp:14:29 bytes 2048

#include <vector>

int main() {
  std::vector<int> a(1024), b(512);
  long s = 0;
  for (int r = 0; r < 2; r++) {
    for (std::size_t i = 0; i < a.size(); i++)
      s += a[i];
    for (std::size_t i = 0; i < b.size(); i++)
      s += b[i];
  }
  return static_cast<int>(s);
}
