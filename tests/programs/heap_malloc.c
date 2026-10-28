// Блок 4096 байт с выравниванием 64 = ровно 64 строки, запись 1024 int.
// Обращения через указатель привязываются к месту выделения (строка 14).
// EXPECT: L1 heap@heap_malloc.c:14 writes 1024
// EXPECT: L1 heap@heap_malloc.c:14 write_misses 64
// EXPECT: L1 heap@heap_malloc.c:14 write_hits 960
// EXPECT: L1 <unknown> accesses 0
// EXPECT: heap heap@heap_malloc.c:14 allocs 1
// EXPECT: heap heap@heap_malloc.c:14 bytes 4096

#include <stdlib.h>

int main(void) {
  int *p;
  p = aligned_alloc(64, 4096);
  for (int i = 0; i < 1024; i++)
    p[i] = i;
  free(p);
  return 0;
}
