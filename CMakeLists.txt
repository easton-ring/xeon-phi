cmake_minimum_required(VERSION 3.13)
project(knc_rf_lab C)

# Portable C99 core: same sources build the host baseline here and the KNC
# native binaries via mic/native/Makefile (icc -mmic).
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(KNC_OPENMP "Build with OpenMP" ON)
option(KNC_NATIVE "Tune for the build host (-march=native)" ON)

set(KNC_FLAGS -O3 -ffp-contract=off -fno-fast-math -Wall -Wextra -pedantic)
if(KNC_NATIVE)
  list(APPEND KNC_FLAGS -march=native)
endif()
if(KNC_OPENMP)
  find_package(OpenMP REQUIRED COMPONENTS C)
  list(APPEND KNC_FLAGS ${OpenMP_C_FLAGS})
endif()
string(REPLACE ";" " " KNC_FLAGS_STR "${KNC_FLAGS}")

execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE KNC_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git status --porcelain --untracked-files=no WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                OUTPUT_VARIABLE KNC_GIT_DIRTY OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT KNC_GIT_COMMIT)
  set(KNC_GIT_COMMIT "unknown")
elseif(KNC_GIT_DIRTY)
  set(KNC_GIT_COMMIT "${KNC_GIT_COMMIT}-dirty")
endif()

set(CORE_SRC csrc/src/crc32.c csrc/src/sha256.c csrc/src/timing.c csrc/src/kncds.c
             csrc/src/kernels.c csrc/src/fft.c csrc/src/features.c csrc/src/model.c)

add_library(knc_obj OBJECT ${CORE_SRC})
set_target_properties(knc_obj PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(knc_obj PUBLIC csrc/include)
target_compile_options(knc_obj PRIVATE ${KNC_FLAGS})

set(KNC_LIBS m)
if(KNC_OPENMP)
  list(APPEND KNC_LIBS OpenMP::OpenMP_C)
endif()

add_library(knc_core STATIC $<TARGET_OBJECTS:knc_obj>)
target_include_directories(knc_core PUBLIC csrc/include)
target_link_libraries(knc_core PUBLIC ${KNC_LIBS})

# Shared library for the Python ctypes cross-checks (tests/test_c_core.py).
add_library(knc_shared SHARED $<TARGET_OBJECTS:knc_obj>)
set_target_properties(knc_shared PROPERTIES OUTPUT_NAME knc)
target_link_libraries(knc_shared PUBLIC ${KNC_LIBS})

foreach(tool knc_validate knc_bench knc_infer)
  if(EXISTS ${CMAKE_SOURCE_DIR}/csrc/tools/${tool}.c)
    add_executable(${tool} csrc/tools/${tool}.c)
    target_compile_options(${tool} PRIVATE ${KNC_FLAGS})
    target_link_libraries(${tool} PRIVATE knc_core)
    target_compile_definitions(${tool} PRIVATE
      KNC_GIT_COMMIT="${KNC_GIT_COMMIT}"
      KNC_BUILD_FLAGS="${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}: ${KNC_FLAGS_STR}"
      KNC_EXEC_MODE="host")
  endif()
endforeach()

enable_testing()
foreach(t test_hash test_kncds test_kernels test_fft test_features test_model)
  add_executable(${t} csrc/tests/${t}.c)
  target_compile_options(${t} PRIVATE ${KNC_FLAGS})
  target_link_libraries(${t} PRIVATE knc_core)
  add_test(NAME ${t} COMMAND ${t} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
endforeach()
