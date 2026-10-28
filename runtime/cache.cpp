#include "cache.h"

#include <stdexcept>

namespace ccs {

static constexpr uint64_t kInvalid = ~0ull;

static bool is_pow2(uint64_t x) { return x && !(x & (x - 1)); }

CacheLevel::CacheLevel(const LevelConfig &c) : cfg(c) {
  if (!is_pow2(c.line) || c.ways == 0 || c.size % (c.line * c.ways) != 0)
    throw std::invalid_argument("bad cache geometry: " + c.name);
  uint64_t sets = c.size / (c.line * c.ways);
  if (!is_pow2(sets))
    throw std::invalid_argument("number of sets must be a power of two: " +
                                c.name);
  set_mask_ = sets - 1;
  tags_.assign(sets * c.ways, kInvalid);
  stamps_.assign(sets * c.ways, 0);
  dirty_.assign(sets * c.ways, 0);
}

bool CacheLevel::access(uint64_t line, bool write, bool *evicted_dirty,
                        uint64_t *dirty_victim) {
  const uint64_t ways = cfg.ways;
  const size_t base = (line & set_mask_) * ways;
  uint64_t *tags = &tags_[base];
  uint64_t *stamps = &stamps_[base];
  uint8_t *dirty = &dirty_[base];
  ++clock_;

  // Пустые слоты имеют метку 0 и выбираются жертвой раньше занятых
  size_t lru = 0;
  for (size_t w = 0; w < ways; ++w) {
    if (tags[w] == line) {
      stamps[w] = clock_;
      dirty[w] |= write;
      return true;
    }
    if (stamps[w] < stamps[lru])
      lru = w;
  }

  *evicted_dirty = tags[lru] != kInvalid && dirty[lru];
  *dirty_victim = tags[lru];
  tags[lru] = line;
  stamps[lru] = clock_;
  dirty[lru] = write;
  return false;
}

void CacheLevel::take_dirty(std::vector<uint64_t> &out) {
  for (size_t i = 0; i < tags_.size(); ++i)
    if (tags_[i] != kInvalid && dirty_[i]) {
      out.push_back(tags_[i]);
      dirty_[i] = 0;
    }
}

Hierarchy::Hierarchy(const std::vector<LevelConfig> &cfgs) {
  if (cfgs.empty())
    throw std::invalid_argument("no cache levels");
  for (const LevelConfig &c : cfgs) {
    if (c.line != cfgs[0].line)
      throw std::invalid_argument("all levels must have the same line size");
    levels.emplace_back(c);
  }
  line_shift_ = __builtin_ctzll(cfgs[0].line);
}

void Hierarchy::resize_vars(size_t n) {
  for (CacheLevel &l : levels)
    l.per_var.resize(n);
}

void Hierarchy::access(uint64_t addr, uint64_t size, bool write,
                       uint32_t var) {
  if (size == 0)
    return;
  const uint64_t first = addr >> line_shift_;
  const uint64_t last = (addr + size - 1) >> line_shift_;
  for (uint64_t line = first; line <= last; ++line)
    access_line(0, line, write, var);
}

void Hierarchy::access_line(size_t lvl, uint64_t line, bool write,
                            uint32_t var) {
  CacheLevel &l = levels[lvl];
  bool evicted_dirty;
  uint64_t victim;
  const bool hit = l.access(line, write, &evicted_dirty, &victim);

  Counters &v = l.per_var[var];
  if (hit) {
    ++(write ? l.total.write_hits : l.total.read_hits);
    ++(write ? v.write_hits : v.read_hits);
    return;
  }
  ++(write ? l.total.write_misses : l.total.read_misses);
  ++(write ? v.write_misses : v.read_misses);

  // При write-allocate строка всё равно сначала читается снизу
  if (lvl + 1 < levels.size())
    access_line(lvl + 1, line, false, var);
  else
    ++mem_reads;

  if (evicted_dirty)
    write_back(lvl + 1, victim);
}

// Запись вытесненной строки целиком: снизу её читать не нужно
void Hierarchy::write_back(size_t lvl, uint64_t line) {
  if (lvl == levels.size()) {
    ++mem_writes;
    return;
  }
  CacheLevel &l = levels[lvl];
  ++l.writebacks_in;
  bool evicted_dirty;
  uint64_t victim;
  if (!l.access(line, true, &evicted_dirty, &victim) && evicted_dirty)
    write_back(lvl + 1, victim);
}

void Hierarchy::flush() {
  std::vector<uint64_t> dirty;
  for (size_t lvl = 0; lvl < levels.size(); ++lvl) {
    dirty.clear();
    levels[lvl].take_dirty(dirty);
    for (uint64_t line : dirty)
      write_back(lvl + 1, line);
  }
}

} // namespace ccs
