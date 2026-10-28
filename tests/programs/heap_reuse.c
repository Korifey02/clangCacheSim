// После free тот же адрес обычно достаётся следующему выделению.
// Обращения должны попасть к тому месту выделения, которому блок принадлежит сейчас.
// EXPECT: L1 heap@heap_reuse.c:17 writes 64
// EXPECT: L1 heap@heap_reuse.c:22 writes 32
// EXPECT: L1 heap@heap_reuse.c:29 writes 16
// EXPECT: L1 <unknown> accesses 0
// EXPECT: heap heap@heap_reuse.c:17 allocs 1
// EXPECT: heap heap@heap_reuse.c:22 allocs 1
// EXPECT: heap heap@heap_reuse.c:28 bytes 1024
// EXPECT: heap heap@heap_reuse.c:29 bytes 8192

#include <stdlib.h>

int main(void) {
  char *p, *q, *r;

  p = malloc(4096);
  for (int i = 0; i < 64; i++)
    p[i * 64] = 1;
  free(p);

  q = malloc(4096);
  for (int i = 0; i < 32; i++)
    q[i * 64] = 2;
  free(q);

  // realloc переносит блок в новое место выделения
  r = malloc(1024);
  r = realloc(r, 8192);
  for (int i = 0; i < 16; i++)
    r[i * 512] = 3;
  free(r);
  return 0;
}
