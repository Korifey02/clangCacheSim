// memcpy 4096 байт: одно чтение источника и одна запись приёмника,
// каждое разбивается на 64 обращения к строкам.
// EXPECT: L1 - accesses 128
// EXPECT: L1 src accesses 64
// EXPECT: L1 src read_misses 64
// EXPECT: L1 dst accesses 64
// EXPECT: L1 dst write_misses 64

#include <string.h>

char src[4096] __attribute__((aligned(64)));
char dst[4096] __attribute__((aligned(64)));

int main(void) {
  memcpy(dst, src, sizeof dst);
  return 0;
}
