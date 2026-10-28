#include "heap.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <fstream>
#include <link.h>
#include <map>
#include <mutex>
#include <new>
#include <pthread.h>
#include <set>
#include <unistd.h>
#include <unordered_map>

// Настоящие функции glibc; наши malloc/free ниже подменяют их для всего процесса
extern "C" {
void *__libc_malloc(size_t);
void __libc_free(void *);
void *__libc_calloc(size_t, size_t);
void *__libc_realloc(void *, size_t);
void *__libc_memalign(size_t, size_t);
void *__libc_valloc(size_t);
void *__libc_pvalloc(size_t);
}

namespace {

constexpr int kMaxFrames = 12;

struct Block {
  uint64_t end;
  uint32_t site;
};

struct Site {
  std::vector<uintptr_t> pcs;
  uint32_t slot;
  uint64_t allocs = 0;
  uint64_t bytes = 0;
  std::string source;
};

struct StackHash {
  size_t operator()(const std::vector<uintptr_t> &v) const {
    size_t h = 1469598103934665603ull;
    for (uintptr_t pc : v)
      h = (h ^ pc) * 1099511628211ull;
    return h;
  }
};

struct Heap {
  std::mutex mu;
  std::map<uint64_t, Block> blocks;
  std::vector<Site> sites;
  std::unordered_map<std::vector<uintptr_t>, uint32_t, StackHash> site_by_stack;
  // Последний найденный блок: обращения обычно идут подряд по одному массиву
  uint64_t last_begin = 0;
  uint64_t last_size = 0;
  uint32_t last_slot = 0;
  // Стек главного потока: такие адреса точно не из кучи, дерево не трогаем
  uint64_t stack_lo = 0;
  uint64_t stack_hi = 0;
};

alignas(Heap) unsigned char g_heap_storage[sizeof(Heap)];
Heap *g_heap = nullptr;

// Выделения самого рантайма не отслеживаются
thread_local bool t_busy = false;

struct Busy {
  bool prev;
  Busy() : prev(t_busy) { t_busy = true; }
  ~Busy() { t_busy = prev; }
};

// Выделения до этого момента (загрузчик, инициализация libc) не отслеживаются
__attribute__((constructor(101))) void heap_init() {
  Busy b;
  Heap *h = new (g_heap_storage) Heap;
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    void *addr;
    size_t size;
    if (pthread_attr_getstack(&attr, &addr, &size) == 0) {
      h->stack_lo = reinterpret_cast<uint64_t>(addr);
      h->stack_hi = h->stack_lo + size;
    }
    pthread_attr_destroy(&attr);
  }
  // Первый вызов backtrace загружает libgcc_s и сам выделяет память
  void *warmup[2];
  backtrace(warmup, 2);
  g_heap = h;
}

void on_alloc(void *p, size_t n, void *ra) {
  Heap *h = g_heap;
  if (!p || !h || t_busy)
    return;
  Busy b;

  void *frames[kMaxFrames + 8];
  const int k = backtrace(frames, kMaxFrames + 8);
  // Кадры самого перехватчика отрезаем: стек начинается с того, кто вызвал malloc
  int from = 0;
  for (int i = 0; i < k; ++i)
    if (frames[i] == ra) {
      from = i;
      break;
    }
  const int to = std::min(k, from + kMaxFrames);
  std::vector<uintptr_t> stack;
  for (int i = from; i < to; ++i)
    stack.push_back(reinterpret_cast<uintptr_t>(frames[i]));

  std::lock_guard<std::mutex> lock(h->mu);
  auto [it, inserted] =
      h->site_by_stack.try_emplace(std::move(stack), h->sites.size());
  if (inserted) {
    Site s;
    s.pcs = it->first;
    s.slot = ccs::new_slot("heap#" + std::to_string(h->sites.size()));
    h->sites.push_back(std::move(s));
  }
  Site &s = h->sites[it->second];
  ++s.allocs;
  s.bytes += n;

  const uint64_t begin = reinterpret_cast<uint64_t>(p);
  h->blocks[begin] = {begin + (n ? n : 1), it->second};
  if (h->last_begin == begin)
    h->last_size = 0;
}

void on_free(void *p) {
  Heap *h = g_heap;
  if (!p || !h || t_busy)
    return;
  Busy b;
  std::lock_guard<std::mutex> lock(h->mu);
  const uint64_t begin = reinterpret_cast<uint64_t>(p);
  h->blocks.erase(begin);
  if (h->last_begin == begin)
    h->last_size = 0;
}

struct Frame {
  std::string func;
  std::string file;
  unsigned line = 0;
  unsigned col = 0;
};

bool is_user_frame(const Frame &f) {
  if (f.line == 0 || f.file.empty() || f.file == "??")
    return false;
  return f.file.rfind("/usr/", 0) != 0 && f.file.rfind("/lib", 0) != 0;
}

std::string self_exe() {
  char buf[PATH_MAX];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
  return n > 0 ? std::string(buf, n) : std::string();
}

// "модуль" и адрес внутри него, как их ждёт llvm-symbolizer
bool module_address(uintptr_t pc, std::string *module, uintptr_t *addr) {
  Dl_info info;
  if (!dladdr(reinterpret_cast<void *>(pc), &info) || !info.dli_fbase)
    return false;
  Dl_info self;
  dladdr(reinterpret_cast<void *>(&heap_init), &self);
  *module = info.dli_fbase == self.dli_fbase || !info.dli_fname
                ? self_exe()
                : std::string(info.dli_fname);
  const auto *eh = static_cast<const ElfW(Ehdr) *>(info.dli_fbase);
  const uintptr_t base = reinterpret_cast<uintptr_t>(info.dli_fbase);
  *addr = eh->e_type == ET_DYN ? pc - base : pc;
  return !module->empty();
}

bool parse_location(const std::string &s, Frame &f) {
  const size_t c2 = s.rfind(':');
  if (c2 == std::string::npos)
    return false;
  const size_t c1 = s.rfind(':', c2 - 1);
  if (c1 == std::string::npos)
    return false;
  f.file = s.substr(0, c1);
  f.line = std::strtoul(s.c_str() + c1 + 1, nullptr, 10);
  f.col = std::strtoul(s.c_str() + c2 + 1, nullptr, 10);
  return true;
}

// Адрес возврата указывает на инструкцию после call, поэтому спрашиваем pc - 1
std::map<uintptr_t, std::vector<Frame>>
symbolize(const std::vector<uintptr_t> &pcs) {
  std::map<uintptr_t, std::vector<Frame>> result;
  const char *tool = std::getenv("CCS_SYMBOLIZER");
  if (!tool || !*tool)
    tool = CCS_SYMBOLIZER;

  char path[] = "/tmp/ccs-symbolize-XXXXXX";
  const int fd = mkstemp(path);
  if (fd < 0)
    return result;
  FILE *q = fdopen(fd, "w");
  std::vector<uintptr_t> order;
  for (uintptr_t pc : pcs) {
    std::string module;
    uintptr_t addr;
    if (!module_address(pc - 1, &module, &addr))
      continue;
    std::fprintf(q, "\"%s\" 0x%lx\n", module.c_str(), (unsigned long)addr);
    order.push_back(pc);
  }
  std::fclose(q);

  const std::string cmd = std::string("'") + tool +
                          "' --inlining --demangle < " + path + " 2>/dev/null";
  if (FILE *out = popen(cmd.c_str(), "r")) {
    char line[4096];
    size_t index = 0;
    std::vector<Frame> frames;
    Frame cur;
    bool want_func = true;
    while (std::fgets(line, sizeof line, out) && index < order.size()) {
      std::string s(line);
      while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
        s.pop_back();
      if (s.empty()) {
        result[order[index++]] = std::move(frames);
        frames.clear();
        want_func = true;
      } else if (want_func) {
        cur = Frame{};
        cur.func = s;
        want_func = false;
      } else {
        parse_location(s, cur);
        frames.push_back(cur);
        want_func = true;
      }
    }
    pclose(out);
  }
  unlink(path);
  return result;
}

std::string source_line(const std::string &file, unsigned line) {
  std::ifstream in(file);
  std::string s;
  for (unsigned i = 0; i < line && std::getline(in, s); ++i) {
  }
  const size_t b = s.find_first_not_of(" \t");
  if (b == std::string::npos)
    return "";
  s = s.substr(b);
  if (s.size() > 80)
    s = s.substr(0, 77) + "...";
  return s;
}

std::string basename_of(const std::string &path) {
  const size_t slash = path.rfind('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace

namespace ccs {

HeapPause::HeapPause() : prev(t_busy) { t_busy = true; }
HeapPause::~HeapPause() { t_busy = prev; }

// Без блокировки: рантайм пока рассчитан на однопоточные программы (ROADMAP 5.0)
uint32_t heap_slot(uint64_t addr) {
  Heap *h = g_heap;
  if (!h)
    return 0;
  if (addr - h->last_begin < h->last_size)
    return h->last_slot;
  if (addr - h->stack_lo < h->stack_hi - h->stack_lo)
    return 0;
  auto it = h->blocks.upper_bound(addr);
  if (it == h->blocks.begin())
    return 0;
  --it;
  if (addr >= it->second.end)
    return 0;
  h->last_begin = it->first;
  h->last_size = it->second.end - it->first;
  h->last_slot = h->sites[it->second.site].slot;
  return h->last_slot;
}

void heap_resolve_names(std::vector<std::string> &names) {
  Heap *h = g_heap;
  if (!h || h->sites.empty())
    return;
  Busy b;
  std::lock_guard<std::mutex> lock(h->mu);

  std::set<uintptr_t> unique;
  for (const Site &s : h->sites)
    unique.insert(s.pcs.begin(), s.pcs.end());
  const std::vector<uintptr_t> pcs(unique.begin(), unique.end());
  const auto frames = symbolize(pcs);

  // Первый кадр из кода пользователя, иначе — первая известная функция
  std::vector<Frame> picks(h->sites.size());
  for (size_t i = 0; i < h->sites.size(); ++i) {
    const Frame *user = nullptr, *any = nullptr;
    for (uintptr_t pc : h->sites[i].pcs) {
      auto it = frames.find(pc);
      if (it == frames.end())
        continue;
      for (const Frame &f : it->second) {
        if (!any && f.func != "??")
          any = &f;
        if (is_user_frame(f)) {
          user = &f;
          break;
        }
      }
      if (user)
        break;
    }
    if (user)
      picks[i] = *user;
    else if (any)
      picks[i].func = any->func;
  }

  // Столбец добавляем, только если на одной строке несколько мест выделения
  std::map<std::string, std::set<unsigned>> cols;
  for (const Frame &f : picks)
    if (f.line)
      cols[basename_of(f.file) + ":" + std::to_string(f.line)].insert(f.col);

  for (size_t i = 0; i < h->sites.size(); ++i) {
    Site &s = h->sites[i];
    const Frame &f = picks[i];
    std::string name;
    if (f.line) {
      const std::string key = basename_of(f.file) + ":" + std::to_string(f.line);
      name = "heap@" + key;
      if (cols[key].size() > 1)
        name += ":" + std::to_string(f.col);
      s.source = source_line(f.file, f.line);
    } else if (!f.func.empty()) {
      name = "heap@" + f.func;
    } else {
      continue;
    }
    names[s.slot] = name;
  }
}

namespace {
struct SiteTotal {
  uint64_t allocs = 0;
  uint64_t bytes = 0;
  std::string source;
};

std::vector<std::pair<std::string, SiteTotal>>
site_totals(const std::vector<std::string> &names) {
  std::map<std::string, SiteTotal> by_name;
  if (Heap *h = g_heap)
    for (const Site &s : h->sites) {
      SiteTotal &t = by_name[names[s.slot]];
      t.allocs += s.allocs;
      t.bytes += s.bytes;
      if (t.source.empty())
        t.source = s.source;
    }
  std::vector<std::pair<std::string, SiteTotal>> out(by_name.begin(),
                                                     by_name.end());
  std::sort(out.begin(), out.end(), [](const auto &a, const auto &b) {
    return a.second.bytes > b.second.bytes;
  });
  return out;
}
} // namespace

void heap_print_sites(FILE *f, const std::vector<std::string> &names) {
  const auto totals = site_totals(names);
  if (totals.empty())
    return;
  size_t width = 4;
  for (const auto &[name, t] : totals)
    width = std::max(width, name.size());
  width = std::min<size_t>(width, 40);
  std::fprintf(f, "Heap allocation sites\n");
  std::fprintf(f, "  %-*s %10s %14s  %s\n", int(width), "site", "allocs",
               "bytes", "source");
  for (const auto &[name, t] : totals)
    std::fprintf(f, "  %-*s %10llu %14llu  %s\n", int(width),
                 shorten(name, width).c_str(),
                 (unsigned long long)t.allocs, (unsigned long long)t.bytes,
                 t.source.c_str());
  std::fprintf(f, "\n");
}

void heap_print_sites_tsv(FILE *f, const std::vector<std::string> &names) {
  for (const auto &[name, t] : site_totals(names)) {
    std::fprintf(f, "heap\t%s\tallocs\t%llu\n", name.c_str(),
                 (unsigned long long)t.allocs);
    std::fprintf(f, "heap\t%s\tbytes\t%llu\n", name.c_str(),
                 (unsigned long long)t.bytes);
  }
}

} // namespace ccs

// Подмена распределителя памяти. glibc явно поддерживает такую замену:
// достаточно определить эти функции в программе.
extern "C" {

void *malloc(size_t n) noexcept {
  void *p = __libc_malloc(n);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

void free(void *p) noexcept {
  on_free(p);
  __libc_free(p);
}

void *calloc(size_t count, size_t n) noexcept {
  void *p = __libc_calloc(count, n);
  on_alloc(p, count * n, __builtin_return_address(0));
  return p;
}

void *realloc(void *old, size_t n) noexcept {
  void *p = __libc_realloc(old, n);
  // При неудаче старый блок остаётся живым
  if (p || n == 0)
    on_free(old);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

void *reallocarray(void *old, size_t count, size_t n) noexcept {
  size_t total;
  if (__builtin_mul_overflow(count, n, &total)) {
    errno = ENOMEM;
    return nullptr;
  }
  void *p = __libc_realloc(old, total);
  if (p || total == 0)
    on_free(old);
  on_alloc(p, total, __builtin_return_address(0));
  return p;
}

void *memalign(size_t align, size_t n) noexcept {
  void *p = __libc_memalign(align, n);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

void *aligned_alloc(size_t align, size_t n) noexcept {
  void *p = __libc_memalign(align, n);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

int posix_memalign(void **out, size_t align, size_t n) noexcept {
  if (align % sizeof(void *) != 0 || (align & (align - 1)) != 0)
    return EINVAL;
  void *p = __libc_memalign(align, n);
  if (!p)
    return ENOMEM;
  *out = p;
  on_alloc(p, n, __builtin_return_address(0));
  return 0;
}

void *valloc(size_t n) noexcept {
  void *p = __libc_valloc(n);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

void *pvalloc(size_t n) noexcept {
  void *p = __libc_pvalloc(n);
  on_alloc(p, n, __builtin_return_address(0));
  return p;
}

} // extern "C"
