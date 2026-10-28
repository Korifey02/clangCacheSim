#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace ccs {

// Номер переменной для адреса внутри живого блока кучи, 0 — адрес не из кучи
uint32_t heap_slot(uint64_t addr);

// Перевести стеки выделений в имена вида heap@файл:строка (заменяет names[слот])
void heap_resolve_names(std::vector<std::string> &names);

void heap_print_sites(FILE *f, const std::vector<std::string> &names);
void heap_print_sites_tsv(FILE *f, const std::vector<std::string> &names);

// Создаётся в runtime.cpp: новый номер переменной без слияния по имени
uint32_t new_slot(const std::string &name);

// Имя для текстового отчёта: длинное режется посередине (runtime.cpp)
std::string shorten(const std::string &name, size_t max);

// Пока объект жив, выделения памяти в этом потоке не отслеживаются
struct HeapPause {
  HeapPause();
  ~HeapPause();
  bool prev;
};

} // namespace ccs
