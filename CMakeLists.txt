cmake_minimum_required(VERSION 3.22)
project(pdsynth VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# The engine is plain C and knows nothing about plugins, hosts or windows. It
# is the same code the tests exercise and the same code every format runs.
# ---------------------------------------------------------------------------
add_library(pdcore STATIC
  src/pd_osc.c
  src/pd_env.c
  src/pd_voice.c
  src/pd_os.c
  src/pd_filter.c
  src/pd_cv.c
  src/pd_fx.c
  src/pd_sysex.c
  src/pd_synth.c
  src/pd_presets.c)
target_include_directories(pdcore PUBLIC src)
# -Wall and -Wextra are GCC and Clang spellings. MSVC reads -Wextra as a
# numeric argument and refuses to compile at all, which is what the first CI
# run on Windows found within a minute of existing.
if(MSVC)
  # MSVC leaves M_PI out unless asked, and it is not in the C standard, so
  # every file that wants it says so as well.
  add_compile_definitions(_USE_MATH_DEFINES)
  target_compile_options(pdcore PRIVATE /W3)
else()
  target_compile_options(pdcore PRIVATE -Wall -Wextra)
endif()
if(NOT WIN32)
  target_link_libraries(pdcore PUBLIC m)
endif()

add_executable(pd_render tools/pd_render.c)
target_link_libraries(pd_render PRIVATE pdcore)

# Measures the factory bank: every preset sounds, answers the hand, and sits at
# the level of its neighbours, or this fails.
add_executable(pd_bankcheck tools/pd_bankcheck.c)
target_link_libraries(pd_bankcheck PRIVATE pdcore)
add_test(NAME bank COMMAND pd_bankcheck)

# Asks the harder question: does each preset behave like the thing it is named
# after. A marimba that sustains for four seconds is not a marimba however
# clean it measures.
add_executable(pd_character tools/pd_character.c)
target_link_libraries(pd_character PRIVATE pdcore)
add_test(NAME character COMMAND pd_character)

enable_testing()
foreach(t osc env voice extras cv fx sysex)
  add_executable(test_${t} tests/test_${t}.c)
  target_link_libraries(test_${t} PRIVATE pdcore)
  add_test(NAME ${t} COMMAND test_${t})
endforeach()

# ---------------------------------------------------------------------------
# The plugin. One editor, every format, three platforms.
# ---------------------------------------------------------------------------
option(PDSYNTH_BUILD_PLUGIN "Build the plugin and standalone" ON)
if(PDSYNTH_BUILD_PLUGIN)
  include(FetchContent)

  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(JUCE)

  # CLAP is not a JUCE format, it is bolted on. No tagged release of the
  # extensions supports JUCE 8, so this pins the commit on main that does,
  # which is the same pin the Helm revival uses.
  FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG        main
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(clap-juce-extensions)

  juce_add_plugin(pdsynth
    COMPANY_NAME            "Keith Adler"
    PLUGIN_MANUFACTURER_CODE Kadr
    PLUGIN_CODE             Pdsy
    FORMATS                 AU VST3 LV2 Standalone
    LV2URI                  "https://github.com/keithadler/pdsynth"
    IS_SYNTH                TRUE
    NEEDS_MIDI_INPUT        TRUE
    NEEDS_MIDI_OUTPUT       FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PRODUCT_NAME            "pdsynth")

  target_sources(pdsynth PRIVATE
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/EnvelopeEditor.cpp
    plugin/WaveDisplay.cpp
    plugin/Theme.cpp)

  target_compile_definitions(pdsynth PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

  target_link_libraries(pdsynth PRIVATE
    pdcore
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

  clap_juce_extensions_plugin(TARGET pdsynth
    CLAP_ID "com.keithadler.pdsynth"
    CLAP_FEATURES instrument synthesizer)

  juce_add_console_app(pd_buscheck PRODUCT_NAME "pd_buscheck")
  target_sources(pd_buscheck PRIVATE tools/pd_buscheck.cpp
    plugin/PluginProcessor.cpp plugin/PluginEditor.cpp
    plugin/EnvelopeEditor.cpp plugin/WaveDisplay.cpp plugin/Theme.cpp)
  target_compile_definitions(pd_buscheck PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="pdsynth" JucePlugin_IsSynth=1)
  target_include_directories(pd_buscheck PRIVATE plugin)
  target_link_libraries(pd_buscheck PRIVATE pdcore juce::juce_audio_utils
    juce::juce_gui_basics juce::juce_dsp juce::juce_recommended_config_flags)

  # Takes a Casio dump through the plugin's own parameters and back out, which
  # is the trip a player's patch makes and the one the C tests cannot see.
  juce_add_console_app(pd_syxplugcheck PRODUCT_NAME "pd_syxplugcheck")
  target_sources(pd_syxplugcheck PRIVATE tools/pd_syxplugcheck.cpp
    plugin/PluginProcessor.cpp plugin/PluginEditor.cpp
    plugin/EnvelopeEditor.cpp plugin/WaveDisplay.cpp plugin/Theme.cpp)
  target_compile_definitions(pd_syxplugcheck PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1
    JucePlugin_Name="pdsynth" JucePlugin_IsSynth=1)
  target_include_directories(pd_syxplugcheck PRIVATE plugin)
  target_link_libraries(pd_syxplugcheck PRIVATE pdcore juce::juce_audio_utils
    juce::juce_gui_basics juce::juce_dsp juce::juce_recommended_config_flags)
  add_test(NAME sysex_through_plugin COMMAND pd_syxplugcheck)

  # Draws the editor with no window, so it can be looked at rather than assumed
  juce_add_console_app(pd_uishot PRODUCT_NAME "pd_uishot")
  target_sources(pd_uishot PRIVATE
    tools/pd_uishot.cpp
    plugin/PluginProcessor.cpp
    plugin/PluginEditor.cpp
    plugin/EnvelopeEditor.cpp
    plugin/WaveDisplay.cpp
    plugin/Theme.cpp)
  target_compile_definitions(pd_uishot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
    # the shot has to pump the message loop so the editor's timers run and the
    # meters hold real numbers rather than zeroes
    JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="pdsynth" JucePlugin_IsSynth=1)
  target_include_directories(pd_uishot PRIVATE plugin)
  target_link_libraries(pd_uishot PRIVATE
    pdcore juce::juce_audio_utils juce::juce_gui_basics juce::juce_dsp
    juce::juce_recommended_config_flags)
endif()

add_executable(pd_sysexcheck tools/pd_sysexcheck.c)
target_link_libraries(pd_sysexcheck PRIVATE pdcore)
