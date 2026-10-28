# clangCacheSim

Трассовый симулятор кэша с привязкой к переменным. Программа собирается clang с плагином,
который вставляет вызов рантайма перед каждым обращением к памяти; рантайм моделирует
иерархию кэшей и при выходе печатает статистику по уровням и по переменным.

## Сборка

Нужны clang-23, llvm-23-dev, cmake, ninja. Для Ubuntu 24.04 пакеты берутся из apt.llvm.org:

```sh
sudo curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key -o /etc/apt/keyrings/apt.llvm.org.asc
echo "deb [signed-by=/etc/apt/keyrings/apt.llvm.org.asc] http://apt.llvm.org/noble/ llvm-toolchain-noble-23 main" \
  | sudo tee /etc/apt/sources.list.d/llvm-23.list
sudo apt update && sudo apt install clang-23 lld-23 llvm-23-dev cmake ninja-build
```

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-23
ninja -C build
```

Другая версия LLVM — `-DCCS_LLVM_VERSION=20` (и `clang++-20`).

## Использование

```sh
build/bin/ccs-clang -O2 examples/matmul.c -o matmul
./matmul
```

`ccs-clang` и `ccs-clang++` принимают те же аргументы, что clang. Отчёт пишется в stderr.

Имена в отчёте:
- глобальные переменные — по имени (`c`), локальные — `функция::имя` (`main::a`);
- динамическая память (`malloc`, `new`, `std::vector` и т.д.) — по месту выделения в коде:
  `heap@файл:строка`, а если на строке несколько выделений — `heap@файл:строка:столбец`.
  Место ищется по стеку вызовов: берётся первый кадр не из системных заголовков. Для этого
  рантайм подменяет `malloc`/`free` и при выходе вызывает `llvm-symbolizer`;
- `<unknown>` — адреса, которые пока не удаётся сопоставить (например, объект на стеке,
  пришедший в функцию указателем).

| Настройка | Где | По умолчанию |
|---|---|---|
| `-mllvm -ccs-stage=early\|late` | при компиляции | `early` — обращения как в исходнике; `late` — после всех оптимизаций |
| `CCS_CACHE=name:size:line:ways,...` | при запуске | `L1:32768:64:8,L2:262144:64:8` |
| `CCS_REPORT=путь` | при запуске | stderr |
| `CCS_FORMAT=tsv` | при запуске | текстовый отчёт; `tsv` — строки `уровень, переменная, метрика, значение` |
| `CCS_SYMBOLIZER=путь` | при запуске | `llvm-symbolizer` той же версии LLVM |

## Тесты

```sh
ctest --test-dir build --output-on-failure
```

`tests/programs/*.c` — программы, у которых ожидаемые числа посчитаны вручную и записаны
в строках `// EXPECT: уровень переменная метрика значение`. Конфигурацию кэша для теста
можно задать строкой `// CCS_CACHE: ...`. `tests/unit` — проверки модели кэша без компилятора.

План работ — в [ROADMAP.md](ROADMAP.md).
