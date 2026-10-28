#include "cache.h"
#include "heap.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

// Описатель переменной; плагин создаёт по одному на переменную в модуле
struct ccs_var {
  const char *name;
  uint32_t slot;
};

namespace {

using namespace ccs;
using Clock = std::chrono::steady_clock;

const char *kDefaultConfig = "L1:32768:64:8,L2:262144:64:8";
constexpr size_t kNameWidth = 60;

std::vector<LevelConfig> parse_config(const char *s) {
  std::vector<LevelConfig> out;
  std::string all = s;
  size_t pos = 0;
  while (pos <= all.size()) {
    size_t end = all.find(',', pos);
    if (end == std::string::npos)
      end = all.size();
    std::string item = all.substr(pos, end - pos);
    char name[32];
    unsigned long long size, line, ways;
    if (std::sscanf(item.c_str(), "%31[^:]:%llu:%llu:%llu", name, &size, &line,
                    &ways) != 4) {
      std::fprintf(stderr, "[ccs] bad CCS_CACHE item '%s', expected name:size:line:ways\n",
                   item.c_str());
      std::exit(1);
    }
    out.push_back({name, size, line, ways});
    pos = end + 1;
  }
  return out;
}

struct Runtime {
  Hierarchy cache;
  std::vector<std::string> names{"<unknown>"};
  std::unordered_map<std::string, uint32_t> slot_by_name;
  Clock::time_point start = Clock::now();

  explicit Runtime(const std::vector<LevelConfig> &cfg) : cache(cfg) {
    cache.resize_vars(1);
  }

  // Одна и та же переменная может прийти из разных модулей — сводим по имени
  uint32_t slot_for(ccs_var *v) {
    if (v->slot)
      return v->slot;
    HeapPause pause;
    auto [it, inserted] = slot_by_name.try_emplace(v->name, names.size());
    if (inserted) {
      names.push_back(v->name);
      cache.resize_vars(names.size());
    }
    v->slot = it->second;
    return v->slot;
  }

  uint32_t add_slot(const std::string &name) {
    names.push_back(name);
    cache.resize_vars(names.size());
    return names.size() - 1;
  }

  // Разные стеки выделения могут дать одно место в коде — складываем их счётчики
  void merge_same_names() {
    std::unordered_map<std::string, uint32_t> first;
    for (uint32_t i = 0; i < names.size(); ++i) {
      auto [it, inserted] = first.try_emplace(names[i], i);
      if (inserted)
        continue;
      for (CacheLevel &l : cache.levels) {
        l.per_var[it->second] += l.per_var[i];
        l.per_var[i] = Counters{};
      }
    }
  }
};

// Не уничтожается: к памяти обращаются и деструкторы программы после main
Runtime *g_rt = nullptr;

double pct(uint64_t part, uint64_t whole) {
  return whole ? 100.0 * double(part) / double(whole) : 0.0;
}

void print_level(FILE *f, const Runtime &rt, const CacheLevel &l) {
  const Counters &t = l.total;
  std::fprintf(f, "Cache %s: %llu KiB, %llu-way, line %llu B\n",
               l.cfg.name.c_str(), (unsigned long long)(l.cfg.size / 1024),
               (unsigned long long)l.cfg.ways, (unsigned long long)l.cfg.line);
  std::fprintf(f, "  accesses   %llu\n", (unsigned long long)t.accesses());
  std::fprintf(f, "  hits       %llu (read %llu, write %llu)\n",
               (unsigned long long)t.hits(), (unsigned long long)t.read_hits,
               (unsigned long long)t.write_hits);
  std::fprintf(f, "  misses     %llu (read %llu, write %llu)\n",
               (unsigned long long)t.misses(), (unsigned long long)t.read_misses,
               (unsigned long long)t.write_misses);
  std::fprintf(f, "  miss rate  %.3f%%\n", pct(t.misses(), t.accesses()));
  if (l.writebacks_in)
    std::fprintf(f, "  write-backs from upper level  %llu\n",
                 (unsigned long long)l.writebacks_in);

  std::vector<uint32_t> order;
  for (uint32_t i = 0; i < l.per_var.size(); ++i)
    if (l.per_var[i].accesses())
      order.push_back(i);
  std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
    return l.per_var[a].misses() > l.per_var[b].misses();
  });

  size_t width = 8;
  for (uint32_t i : order)
    width = std::max(width, rt.names[i].size());
  width = std::min(width, kNameWidth);
  std::fprintf(f, "  %-*s %12s %12s %12s %12s %9s\n", int(width), "variable",
               "accesses", "misses", "read miss", "write miss", "miss rate");
  for (uint32_t i : order) {
    const Counters &c = l.per_var[i];
    std::fprintf(f, "  %-*s %12llu %12llu %12llu %12llu %8.3f%%\n", int(width),
                 shorten(rt.names[i], width).c_str(),
                 (unsigned long long)c.accesses(),
                 (unsigned long long)c.misses(),
                 (unsigned long long)c.read_misses,
                 (unsigned long long)c.write_misses,
                 pct(c.misses(), c.accesses()));
  }
  std::fprintf(f, "\n");
}

void tsv_counters(FILE *f, const char *level, const char *var,
                  const Counters &c) {
  const std::pair<const char *, uint64_t> rows[] = {
      {"accesses", c.accesses()},       {"hits", c.hits()},
      {"misses", c.misses()},           {"reads", c.read_hits + c.read_misses},
      {"writes", c.write_hits + c.write_misses},
      {"read_hits", c.read_hits},
      {"write_hits", c.write_hits},     {"read_misses", c.read_misses},
      {"write_misses", c.write_misses},
  };
  for (const auto &[key, value] : rows)
    std::fprintf(f, "%s\t%s\t%s\t%llu\n", level, var, key,
                 (unsigned long long)value);
}

// Строки "уровень<TAB>переменная<TAB>метрика<TAB>значение", "-" — итог по уровню
void report_tsv(FILE *f, const Runtime &rt, uint64_t writes_during_run) {
  for (const CacheLevel &l : rt.cache.levels) {
    const char *level = l.cfg.name.c_str();
    tsv_counters(f, level, "-", l.total);
    std::fprintf(f, "%s\t-\twritebacks_in\t%llu\n", level,
                 (unsigned long long)l.writebacks_in);
    for (uint32_t i = 0; i < l.per_var.size(); ++i)
      if (l.per_var[i].accesses())
        tsv_counters(f, level, rt.names[i].c_str(), l.per_var[i]);
  }
  std::fprintf(f, "mem\t-\treads\t%llu\n", (unsigned long long)rt.cache.mem_reads);
  std::fprintf(f, "mem\t-\twrites\t%llu\n", (unsigned long long)rt.cache.mem_writes);
  std::fprintf(f, "mem\t-\twrites_flush\t%llu\n",
               (unsigned long long)(rt.cache.mem_writes - writes_during_run));
  heap_print_sites_tsv(f, rt.names);
}

void report() {
  HeapPause pause;
  Runtime &rt = *g_rt;
  const double seconds =
      std::chrono::duration<double>(Clock::now() - rt.start).count();
  const uint64_t writes_during_run = rt.cache.mem_writes;
  rt.cache.flush();
  heap_resolve_names(rt.names);
  rt.merge_same_names();

  FILE *f = stderr;
  const char *path = std::getenv("CCS_REPORT");
  if (path && *path) {
    f = std::fopen(path, "w");
    if (!f) {
      std::perror(path);
      f = stderr;
    }
  }

  const char *format = std::getenv("CCS_FORMAT");
  if (format && std::strcmp(format, "tsv") == 0) {
    report_tsv(f, rt, writes_during_run);
    if (f != stderr)
      std::fclose(f);
    return;
  }

  std::fprintf(f, "\n=== clangCacheSim ===\nTime %.4f s\n\n", seconds);
  for (const CacheLevel &l : rt.cache.levels)
    print_level(f, rt, l);
  heap_print_sites(f, rt.names);
  std::fprintf(f, "Memory reads   %llu\n", (unsigned long long)rt.cache.mem_reads);
  std::fprintf(f, "Memory writes  %llu (during run %llu, final flush %llu)\n",
               (unsigned long long)rt.cache.mem_writes,
               (unsigned long long)writes_during_run,
               (unsigned long long)(rt.cache.mem_writes - writes_during_run));
  if (f != stderr)
    std::fclose(f);
}

Runtime &runtime() {
  if (!g_rt) {
    HeapPause pause;
    const char *cfg = std::getenv("CCS_CACHE");
    g_rt = new Runtime(parse_config(cfg && *cfg ? cfg : kDefaultConfig));
    std::atexit(report);
  }
  return *g_rt;
}

} // namespace

std::string ccs::shorten(const std::string &name, size_t max) {
  if (name.size() <= max || max < 8)
    return name;
  const size_t head = (max - 3) / 2;
  const size_t tail = max - 3 - head;
  return name.substr(0, head) + "..." + name.substr(name.size() - tail);
}

uint32_t ccs::new_slot(const std::string &name) {
  HeapPause pause;
  return runtime().add_slot(name);
}

extern "C" void __ccs_access(void *addr, uint64_t size, uint32_t is_write,
                             ccs_var *var) {
  Runtime &rt = runtime();
  const uint64_t a = reinterpret_cast<uint64_t>(addr);
  rt.cache.access(a, size, is_write != 0, var ? rt.slot_for(var) : heap_slot(a));
}
