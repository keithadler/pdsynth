cmake_minimum_required(VERSION 3.16)
project(pdsynth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

add_library(pdcore STATIC
  src/pd_osc.c
  src/pd_env.c
  src/pd_voice.c)
target_include_directories(pdcore PUBLIC src)
if(NOT WIN32)
  target_link_libraries(pdcore PUBLIC m)
endif()

add_executable(pd_render tools/pd_render.c)
target_link_libraries(pd_render PRIVATE pdcore)

enable_testing()
foreach(t osc env voice)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE pdcore)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()
