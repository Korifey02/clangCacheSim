#include "cache.h"

#include <cstdio>
#include <stdexcept>

using namespace ccs;

static int failures = 0;

#define CHECK_EQ(actual, expected)                                             \
  do {                                                                         \
    auto a_ = (actual);                                                        \
    auto e_ = (expected);                                                      \
    if (a_ != e_) {                                                            \
      std::printf("%s:%d: %s = %llu, ожидалось %llu\n", __FILE__, __LINE__,    \
                  #actual, (unsigned long long)a_, (unsigned long long)e_);    \
      ++failures;                                                              \
    }                                                                          \
  } while (0)

// 2 набора по 2 канала; строки 0, 2, 4 попадают в набор 0
static void lru_order() {
  Hierarchy h({{"L1", 256, 64, 2}});
  h.resize_vars(1);
  const uint64_t line = 64;
  h.access(0 * line, 4, false, 0); // промах
  h.access(2 * line, 4, false, 0); // промах
  h.access(0 * line, 4, false, 0); // попадание, 2 становится самой старой
  h.access(4 * line, 4, false, 0); // промах, вытесняет 2
  h.access(0 * line, 4, false, 0); // попадание
  h.access(2 * line, 4, false, 0); // промах
  CHECK_EQ(h.levels[0].total.misses(), 4u);
  CHECK_EQ(h.levels[0].total.hits(), 2u);
  CHECK_EQ(h.mem_reads, 4u);
}

static void line_split() {
  Hierarchy h({{"L1", 32768, 64, 8}});
  h.resize_vars(1);
  h.access(60, 8, false, 0);   // байты 60..67 — строки 0 и 1
  h.access(128, 64, false, 0); // ровно строка 2
  h.access(192, 0, false, 0);  // пустое обращение не считается
  CHECK_EQ(h.levels[0].total.accesses(), 3u);
}

// L1 прямого отображения на 2 набора, строки 0 и 2 конфликтуют
static void writeback_chain() {
  Hierarchy h({{"L1", 128, 64, 1}, {"L2", 1024, 64, 2}});
  h.resize_vars(1);
  h.access(0, 4, true, 0);
  h.access(128, 4, true, 0); // вытесняет грязную строку 0 в L2
  CHECK_EQ(h.levels[0].total.write_misses, 2u);
  CHECK_EQ(h.levels[1].total.read_misses, 2u);
  CHECK_EQ(h.levels[1].writebacks_in, 1u);
  CHECK_EQ(h.mem_writes, 0u);

  h.flush(); // строка 2 из L1 в L2, затем обе грязные строки L2 в память
  CHECK_EQ(h.levels[1].writebacks_in, 2u);
  CHECK_EQ(h.mem_writes, 2u);

  h.flush(); // повторный сброс ничего не пишет
  CHECK_EQ(h.mem_writes, 2u);
}

static void per_var() {
  Hierarchy h({{"L1", 32768, 64, 8}});
  h.resize_vars(3);
  h.access(0, 4, false, 1);
  h.access(4, 4, true, 1);
  h.access(4096, 4, false, 2);
  CHECK_EQ(h.levels[0].per_var[1].accesses(), 2u);
  CHECK_EQ(h.levels[0].per_var[1].read_misses, 1u);
  CHECK_EQ(h.levels[0].per_var[1].write_hits, 1u);
  CHECK_EQ(h.levels[0].per_var[2].misses(), 1u);
  CHECK_EQ(h.levels[0].per_var[0].accesses(), 0u);
}

static void bad_config() {
  int thrown = 0;
  try { Hierarchy h({{"L1", 3000, 64, 8}}); } catch (const std::invalid_argument &) { ++thrown; }
  try { Hierarchy h({{"L1", 32768, 48, 8}}); } catch (const std::invalid_argument &) { ++thrown; }
  try { Hierarchy h({{"L1", 32768, 64, 8}, {"L2", 262144, 128, 8}}); } catch (const std::invalid_argument &) { ++thrown; }
  CHECK_EQ(thrown, 3);
}

int main() {
  lru_order();
  line_split();
  writeback_chain();
  per_var();
  bad_config();
  if (failures)
    std::printf("%d проверок не прошло\n", failures);
  return failures ? 1 : 0;
}
