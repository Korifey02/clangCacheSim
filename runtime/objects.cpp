#include "objects.h"
#include "heap.h"

#include <algorithm>
#include <iterator>
#include <map>

namespace {

struct Range {
  uint64_t end;
  ccs_var *var;
};

struct Table {
  std::map<uint64_t, Range> ranges;
  uint64_t lo = UINT64_MAX;
  uint64_t hi = 0;

  // Новый объект вытесняет все старые, с которыми пересекается
  void insert(uint64_t begin, uint64_t end, ccs_var *var) {
    auto it = ranges.upper_bound(begin);
    if (it != ranges.begin()) {
      auto prev = std::prev(it);
      if (prev->second.end > begin)
        it = prev;
    }
    while (it != ranges.end() && it->first < end)
      it = ranges.erase(it);
    ranges.emplace(begin, Range{end, var});
    lo = std::min(lo, begin);
    hi = std::max(hi, end);
  }

  const Range *find(uint64_t addr, uint64_t *begin) const {
    auto it = ranges.upper_bound(addr);
    if (it == ranges.begin())
      return nullptr;
    --it;
    if (addr >= it->second.end)
      return nullptr;
    *begin = it->first;
    return &it->second;
  }
};

// Таблицы заполняются из конструкторов модулей, которые могут выполниться раньше
// инициализации статических объектов этого файла, поэтому создаются при первом
// обращении и никогда не уничтожаются
Table &globals() {
  static Table *t = [] {
    ccs::HeapPause pause;
    return new Table;
  }();
  return *t;
}

Table &stack() {
  static Table *t = [] {
    ccs::HeapPause pause;
    return new Table;
  }();
  return *t;
}

struct LastHit {
  uint64_t begin = 0;
  uint64_t size = 0;
  ccs_var *var = nullptr;
} g_last;

} // namespace

namespace ccs {

// Без блокировки: рантайм пока рассчитан на однопоточные программы (ROADMAP 5.0)
ccs_var *find_object(uint64_t addr) {
  if (addr - g_last.begin < g_last.size)
    return g_last.var;

  const Table *t;
  if (main_stack_contains(addr)) {
    t = &stack();
  } else {
    const Table &g = globals();
    if (addr < g.lo || addr >= g.hi)
      return nullptr;
    t = &g;
  }
  uint64_t begin;
  const Range *r = t->find(addr, &begin);
  if (!r)
    return nullptr;
  g_last = {begin, r->end - begin, r->var};
  return r->var;
}

} // namespace ccs

extern "C" void __ccs_register_global(void *addr, uint64_t size, ccs_var *var) {
  ccs::HeapPause pause;
  const uint64_t begin = reinterpret_cast<uint64_t>(addr);
  globals().insert(begin, begin + size, var);
}

// Вызывается при входе в функцию. Снимать регистрацию при выходе не нужно:
// кадры ниже текущего уже завершились, их записи удаляются здесь же, а запись
// нового объекта вытесняет старые записи на том же месте стека
extern "C" void __ccs_register_stack(void *addr, uint64_t size, ccs_var *var) {
  ccs::HeapPause pause;
  Table &t = stack();
  const uint64_t frame = reinterpret_cast<uint64_t>(__builtin_frame_address(0));
  while (!t.ranges.empty() && t.ranges.begin()->second.end <= frame)
    t.ranges.erase(t.ranges.begin());
  const uint64_t begin = reinterpret_cast<uint64_t>(addr);
  t.insert(begin, begin + size, var);
  g_last.size = 0;
}
