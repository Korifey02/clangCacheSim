// Внутри метода объект виден только через this. Объект лежит на стеке main.
// Прямые обращения: total = 0, 256 записей data и чтение total в return — 258.
// Через this в add: чтение data[i], чтение и запись total — 3 * 256 = 768.
// EXPECT: L1 main::a accesses 1026
// EXPECT: L1 main::a writes 513
// EXPECT: L1 <unknown> accesses 0

struct Acc {
  int data[256];
  long total;
  __attribute__((noinline)) void add(int i) { total += data[i]; }
};

int main() {
  Acc a;
  a.total = 0;
  for (int i = 0; i < 256; i++)
    a.data[i] = i;
  for (int i = 0; i < 256; i++)
    a.add(i);
  return static_cast<int>(a.total & 1);
}
