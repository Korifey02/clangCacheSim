#pragma once
#include <cstdint>

// Описатель переменной; плагин создаёт по одному на переменную в модуле
// (тип { ptr, i32 } в pass/CacheSimPass.cpp)
struct ccs_var {
  const char *name;
  uint32_t slot;
};
