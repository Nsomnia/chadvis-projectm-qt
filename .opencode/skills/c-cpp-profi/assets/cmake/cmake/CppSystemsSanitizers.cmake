include_guard(GLOBAL)

function(cpp_systems_require_clang_or_gnu)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND
     NOT CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "cpp_systems sanitizer helpers require Clang or GCC-style sanitizer flags")
  endif()
endfunction()

function(cpp_systems_enable_sanitizers target)
  cpp_systems_require_clang_or_gnu()
  cmake_parse_arguments(CPP_SYSTEMS "" "" "SANITIZERS" ${ARGN})
  if(NOT CPP_SYSTEMS_SANITIZERS)
    set(CPP_SYSTEMS_SANITIZERS address undefined)
  endif()
  string(REPLACE ";" "," sanitizer_list "${CPP_SYSTEMS_SANITIZERS}")
  target_compile_options("${target}" PRIVATE
    "-fsanitize=${sanitizer_list}"
    -fno-omit-frame-pointer
    -g)
  target_link_options("${target}" PRIVATE "-fsanitize=${sanitizer_list}")
endfunction()

function(cpp_systems_enable_libfuzzer target)
  cpp_systems_require_clang_or_gnu()
  cmake_parse_arguments(CPP_SYSTEMS "" "" "SANITIZERS" ${ARGN})
  if(NOT CPP_SYSTEMS_SANITIZERS)
    set(CPP_SYSTEMS_SANITIZERS address undefined)
  endif()
  list(PREPEND CPP_SYSTEMS_SANITIZERS fuzzer)
  string(REPLACE ";" "," sanitizer_list "${CPP_SYSTEMS_SANITIZERS}")
  target_compile_options("${target}" PRIVATE
    "-fsanitize=${sanitizer_list}"
    -fno-omit-frame-pointer
    -g
    -O1)
  target_link_options("${target}" PRIVATE "-fsanitize=${sanitizer_list}")
endfunction()
