cmake_minimum_required(VERSION 3.20)
project(clangCacheSim CXX)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION}: ${LLVM_DIR}")
if(NOT LLVM_VERSION_MAJOR EQUAL 20)
  message(FATAL_ERROR "Нужен LLVM 20, найден ${LLVM_PACKAGE_VERSION}. Укажите -DLLVM_DIR=/usr/lib/llvm-20/lib/cmake/llvm")
endif()

# Плагин грузится внутрь clang, поэтому собирается с теми же флагами, что и LLVM
add_library(CacheSimPass MODULE pass/CacheSimPass.cpp)
target_include_directories(CacheSimPass SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
separate_arguments(CCS_LLVM_DEFS NATIVE_COMMAND ${LLVM_DEFINITIONS})
target_compile_definitions(CacheSimPass PRIVATE ${CCS_LLVM_DEFS})
if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(CacheSimPass PRIVATE -fno-rtti)
endif()

add_library(ccs_rt STATIC runtime/cache.cpp runtime/runtime.cpp)
set_target_properties(ccs_rt PROPERTIES POSITION_INDEPENDENT_CODE ON)

# build/bin/ccs-clang и ccs-clang++ — clang с подключённым плагином и рантаймом
set(CCS_PLUGIN $<TARGET_FILE:CacheSimPass>)
set(CCS_RUNTIME $<TARGET_FILE:ccs_rt>)
foreach(driver clang clang++)
  set(CCS_COMPILER ${LLVM_TOOLS_BINARY_DIR}/${driver})
  configure_file(tools/ccs-cc.in ${CMAKE_BINARY_DIR}/ccs-${driver}.in @ONLY)
  file(GENERATE
    OUTPUT ${CMAKE_BINARY_DIR}/bin/ccs-${driver}
    INPUT ${CMAKE_BINARY_DIR}/ccs-${driver}.in
    FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()

# build/bin/ccs-run файл.c — собрать, запустить и показать отчёт одной командой
set(CCS_BIN_DIR ${CMAKE_BINARY_DIR}/bin)
configure_file(tools/ccs-run.in ${CCS_BIN_DIR}/ccs-run @ONLY
  FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

enable_testing()

add_executable(cache_test tests/unit/cache_test.cpp runtime/cache.cpp)
target_include_directories(cache_test PRIVATE runtime)
add_test(NAME unit/cache COMMAND cache_test)

file(GLOB CCS_PROGRAM_TESTS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/tests/programs/*.c)
foreach(src ${CCS_PROGRAM_TESTS})
  get_filename_component(name ${src} NAME_WE)
  add_test(NAME program/${name}
    COMMAND sh ${CMAKE_SOURCE_DIR}/tests/run_program_test.sh ${CCS_BIN_DIR} ${src})
endforeach()
