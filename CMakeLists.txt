cmake_minimum_required(VERSION 3.16)
project(pdsynth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

if(APPLE)
  set(PD_MIDI src/pd_midi_coremidi.c)
else()
  set(PD_MIDI src/pd_midi_null.c)
endif()

add_library(pdcore STATIC
  src/pd_osc.c
  src/pd_env.c
  src/pd_voice.c
  ${PD_MIDI})
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

# --- Standalone: a window, a keyboard, and the two lines drawn as they move ---
find_package(SDL2 QUIET)
if(SDL2_FOUND OR TARGET SDL2::SDL2)
  add_library(pdpanel STATIC standalone/pd_panel.c)
  target_include_directories(pdpanel PUBLIC standalone ${SDL2_INCLUDE_DIRS})
  if(TARGET SDL2::SDL2)
    target_link_libraries(pdpanel PUBLIC pdcore SDL2::SDL2)
  else()
    target_link_libraries(pdpanel PUBLIC pdcore ${SDL2_LIBRARIES})
  endif()

  add_executable(pdsynth standalone/pd_standalone.c)
  target_link_libraries(pdsynth PRIVATE pdpanel)
  if(APPLE)
    target_link_libraries(pdsynth PRIVATE "-framework CoreMIDI" "-framework CoreFoundation")
  endif()

  # Draws the panel with no window, so it can be looked at
  add_executable(pd_shot tools/pd_shot.c)
  target_link_libraries(pd_shot PRIVATE pdpanel)
else()
  message(STATUS "SDL2 not found: building without the standalone")
endif()
