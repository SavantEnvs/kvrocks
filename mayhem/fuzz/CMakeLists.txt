# Mayhem fuzz build for kvrocks.
#
# This wraps upstream's own CMake project (add_subdirectory of the repo root), so the dependencies
# (rocksdb, libevent, luajit, ...) and upstream's `unittest` are built exactly as upstream builds
# them, with the normal (uninstrumented) flags. The fuzz harnesses link `kvrocks_fuzz_objs`: the
# same sources and usage requirements as upstream's `kvrocks_objs`, compiled a second time with
# the sanitizer, coverage and DWARF-3 flags (KVFUZZ_FLAGS) so only kvrocks itself is instrumented.
cmake_minimum_required(VERSION 3.16)
project(kvrocks_mayhem LANGUAGES C CXX)

set(KVFUZZ_FLAGS "" CACHE STRING "sanitizer + debug flags for kvrocks sources and the harnesses")
set(KVFUZZ_ENGINE "-fsanitize=fuzzer" CACHE STRING "fuzzing engine link flag")
set(KVFUZZ_STANDALONE_MAIN "" CACHE FILEPATH "LLVM StandaloneFuzzTargetMain.c")
set(KVFUZZ_LSAN_OFF "" CACHE FILEPATH "TU that turns LeakSanitizer off")

get_filename_component(KVROCKS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_subdirectory("${KVROCKS_ROOT}" kvrocks)
# Imported targets kvrocks_objs links (Threads::Threads) are scoped to upstream's directory.
find_package(Threads REQUIRED)

separate_arguments(KVFUZZ_FLAGS_LIST UNIX_COMMAND "${KVFUZZ_FLAGS}")
separate_arguments(KVFUZZ_ENGINE_LIST UNIX_COMMAND "${KVFUZZ_ENGINE}")

# kvrocks_objs, again, instrumented.
get_target_property(_srcs kvrocks_objs SOURCES)
get_target_property(_incs kvrocks_objs INCLUDE_DIRECTORIES)
get_target_property(_defs kvrocks_objs COMPILE_DEFINITIONS)
get_target_property(_opts kvrocks_objs COMPILE_OPTIONS)
get_target_property(_libs kvrocks_objs LINK_LIBRARIES)
list(REMOVE_ITEM _libs PUBLIC PRIVATE INTERFACE)
set(_abs_srcs)
foreach(_s IN LISTS _srcs)
  if(IS_ABSOLUTE "${_s}")
    list(APPEND _abs_srcs "${_s}")
  else()
    list(APPEND _abs_srcs "${KVROCKS_ROOT}/${_s}")
  endif()
endforeach()
add_library(kvrocks_fuzz_objs OBJECT ${_abs_srcs})
target_include_directories(kvrocks_fuzz_objs PUBLIC ${_incs})
target_compile_definitions(kvrocks_fuzz_objs PUBLIC ${_defs})
target_compile_options(kvrocks_fuzz_objs PUBLIC ${_opts})
target_compile_features(kvrocks_fuzz_objs PUBLIC cxx_std_20)
target_link_libraries(kvrocks_fuzz_objs PUBLIC ${_libs})
target_compile_options(kvrocks_fuzz_objs PRIVATE ${KVFUZZ_FLAGS_LIST} -fsanitize=fuzzer-no-link)
# kvrocks needs version.h from upstream's binary dir (configure_file in its CMakeLists).
add_dependencies(kvrocks_fuzz_objs kvrocks_objs)

add_library(kvfuzz_lsan_off OBJECT "${KVFUZZ_LSAN_OFF}")
target_compile_options(kvfuzz_lsan_off PRIVATE ${KVFUZZ_FLAGS_LIST})
add_library(kvfuzz_standalone_main OBJECT "${KVFUZZ_STANDALONE_MAIN}")
set_source_files_properties("${KVFUZZ_STANDALONE_MAIN}" PROPERTIES LANGUAGE C)
target_compile_options(kvfuzz_standalone_main PRIVATE ${KVFUZZ_FLAGS_LIST})

set(KVFUZZ_TARGETS resp rdb_load lua json ft_sql ft_query cluster_nodes config)
set(KVFUZZ_ALL)
foreach(_t IN LISTS KVFUZZ_TARGETS)
  foreach(_kind fuzz standalone)
    if(_kind STREQUAL "fuzz")
      set(_exe kvrocks_fuzz_${_t})
    else()
      set(_exe kvrocks_fuzz_${_t}-standalone)
    endif()
    if(_t STREQUAL "config")
      add_executable(${_exe} fuzz_${_t}.cc)
    else()
      add_executable(${_exe} fuzz_${_t}.cc kvrocks_fuzz_fixture.cc)
    endif()
    target_compile_options(${_exe} PRIVATE ${KVFUZZ_FLAGS_LIST})
    target_link_libraries(${_exe} PRIVATE kvrocks_fuzz_objs kvfuzz_lsan_off ${_libs})
    if(_kind STREQUAL "fuzz")
      target_compile_options(${_exe} PRIVATE -fsanitize=fuzzer-no-link)
      target_link_options(${_exe} PRIVATE ${KVFUZZ_FLAGS_LIST} ${KVFUZZ_ENGINE_LIST})
    else()
      target_link_libraries(${_exe} PRIVATE kvfuzz_standalone_main)
      target_link_options(${_exe} PRIVATE ${KVFUZZ_FLAGS_LIST})
    endif()
    list(APPEND KVFUZZ_ALL ${_exe})
  endforeach()
endforeach()
add_custom_target(kvrocks_fuzzers DEPENDS ${KVFUZZ_ALL})
