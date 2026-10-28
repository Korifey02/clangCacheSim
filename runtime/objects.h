#pragma once
#include "var.h"

#include <cstdint>

namespace ccs {

// Глобальная или стековая переменная, которой принадлежит адрес, или nullptr
ccs_var *find_object(uint64_t addr);

} // namespace ccs
