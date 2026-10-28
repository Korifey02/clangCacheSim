#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ccs {

struct LevelConfig {
  std::string name;
  uint64_t size;
  uint64_t line;
  uint64_t ways;
};

struct Counters {
  uint64_t read_hits = 0;
  uint64_t write_hits = 0;
  uint64_t read_misses = 0;
  uint64_t write_misses = 0;

  uint64_t hits() const { return read_hits + write_hits; }
  uint64_t misses() const { return read_misses + write_misses; }
  uint64_t accesses() const { return hits() + misses(); }
};

// Один уровень: множественно-ассоциативный, LRU, write-back, write-allocate.
// Оперирует номерами строк (адрес >> log2(line)).
class CacheLevel {
public:
  explicit CacheLevel(const LevelConfig &cfg);

  // true — попадание. При промахе строка занимает место вытесненной;
  // если та была грязной, её номер возвращается в *dirty_victim.
  bool access(uint64_t line, bool write, bool *evicted_dirty,
              uint64_t *dirty_victim);
  void take_dirty(std::vector<uint64_t> &out);

  LevelConfig cfg;
  Counters total;
  std::vector<Counters> per_var;
  uint64_t writebacks_in = 0;

private:
  uint64_t set_mask_;
  uint64_t clock_ = 0;
  std::vector<uint64_t> tags_;
  std::vector<uint64_t> stamps_;
  std::vector<uint8_t> dirty_;
};

class Hierarchy {
public:
  explicit Hierarchy(const std::vector<LevelConfig> &cfgs);

  void access(uint64_t addr, uint64_t size, bool write, uint32_t var);
  void resize_vars(size_t n);
  // Сбросить грязные строки вниз до памяти, как при завершении программы
  void flush();

  std::vector<CacheLevel> levels;
  uint64_t mem_reads = 0;
  uint64_t mem_writes = 0;

private:
  void access_line(size_t lvl, uint64_t line, bool write, uint32_t var);
  void write_back(size_t lvl, uint64_t line);

  unsigned line_shift_;
};

} // namespace ccs
