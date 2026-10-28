// x и y живут в разных блоках, поэтому компилятор кладёт их на одно место стека.
// Регистрация в момент начала жизни объекта должна развести их обращения.
// EXPECT: L1 main::x writes 64
// EXPECT: L1 main::y writes 64
// EXPECT: L1 <unknown> accesses 0

struct Buf {
  int d[64];
};

__attribute__((noinline)) void fill(Buf *b, int v) {
  for (int i = 0; i < 64; i++)
    b->d[i] = v;
}

int main() {
  {
    Buf x;
    fill(&x, 1);
  }
  {
    Buf y;
    fill(&y, 2);
  }
  return 0;
}
