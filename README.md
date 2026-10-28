# clangCacheSim

Трассовый симулятор кэша с привязкой к переменным. Программа собирается clang с плагином,
который вставляет вызов рантайма перед каждым обращением к памяти; рантайм моделирует
иерархию кэшей и при выходе печатает статистику по уровням и по переменным.

## Сборка

Нужны clang-20, llvm-20-dev, cmake, ninja.

```sh
cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=clang++-20 -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm
ninja -C build
```

## Использование

```sh
build/bin/ccs-clang -O2 examples/matmul.c -o matmul
./matmul
```

`ccs-clang` и `ccs-clang++` принимают те же аргументы, что clang. Отчёт пишется в stderr.

| Настройка | Где | По умолчанию |
|---|---|---|
| `-mllvm -ccs-stage=early\|late` | при компиляции | `early` — обращения как в исходнике; `late` — после всех оптимизаций |
| `CCS_CACHE=name:size:line:ways,...` | при запуске | `L1:32768:64:8,L2:262144:64:8` |
| `CCS_REPORT=путь` | при запуске | stderr |
| `CCS_FORMAT=tsv` | при запуске | текстовый отчёт; `tsv` — строки `уровень, переменная, метрика, значение` |

## Тесты

```sh
ctest --test-dir build --output-on-failure
```

`tests/programs/*.c` — программы, у которых ожидаемые числа посчитаны вручную и записаны
в строках `// EXPECT: уровень переменная метрика значение`. Конфигурацию кэша для теста
можно задать строкой `// CCS_CACHE: ...`. `tests/unit` — проверки модели кэша без компилятора.

План работ — в [ROADMAP.md](ROADMAP.md).
