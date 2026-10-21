cmake_minimum_required(VERSION 3.22)

# Universal binary + macOS 12 floor. These must be set BEFORE project().
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

# The version shown on the About screen comes from here.
project(UnofficialSpotykach VERSION 0.6.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Short git hash, so the About screen can prove exactly which build is installed.
set(US_BUILD_ID "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE US_GIT_SHA
                  OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)
  if(US_GIT_SHA)
    set(US_BUILD_ID "${US_GIT_SHA}")
  endif()
endif()

include(FetchContent)

# ---- Spotykach core + engine wrapper (no JUCE needed; also built by the headless tests) ----
FetchContent_Declare(DaisySP
  GIT_REPOSITORY https://github.com/electro-smith/DaisySP.git
  GIT_TAG 599511b740f8f3a9b8db72a0642aa45b8a23c3a3
  GIT_SUBMODULES "")   # never fetch the LGPL submodule
FetchContent_MakeAvailable(DaisySP)
# -fwrapv: DaisySP's WhiteNoise is a linear congruential generator that relies on signed integer wrap-around.
target_compile_options(DaisySP PRIVATE -w -fwrapv)

file(GLOB SPK_CORE_SRC CONFIGURE_DEPENDS
  third_party/spotykach/src/core/*.cpp
  third_party/spotykach/src/core/fx/*.cpp)
add_library(spotykach_core STATIC ${SPK_CORE_SRC} src/engine/Engine.cpp)
target_include_directories(spotykach_core PUBLIC
  src/engine
  src/engine/shim
  third_party/spotykach/src
  third_party/spotykach/src/core)
# The upstream code relies on headers being pulled in transitively by libDaisy; force them.
target_compile_options(spotykach_core PRIVATE
  "SHELL:-include cstdint" "SHELL:-include cstddef" "SHELL:-include cmath"
  "SHELL:-include cstring" "SHELL:-include algorithm" -w -fwrapv)
target_link_libraries(spotykach_core PUBLIC DaisySP)
set_target_properties(spotykach_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Headless engine test (not part of the default build).
add_executable(engine_test EXCLUDE_FROM_ALL tests/engine_test.cpp)
target_compile_definitions(engine_test PRIVATE COUNT_ALLOCS)
target_link_libraries(engine_test PRIVATE spotykach_core)

if(US_ENGINE_ONLY)
  return()
endif()

# ---- JUCE plug-in ----
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.16-security
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(UnofficialSpotykach
  COMPANY_NAME "particledots"
  BUNDLE_ID com.particledots.unofficialspotykach
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE Spky
  FORMATS AU VST3
  PRODUCT_NAME "Unofficial Spotykach"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  # Lets Logic offer it in a "MIDI-controlled effect" slot so it can receive MIDI.
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx
  COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(UnofficialSpotykach PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp)

target_compile_definitions(UnofficialSpotykach PRIVATE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  US_BUILD_ID="${US_BUILD_ID}")

target_link_libraries(UnofficialSpotykach PRIVATE
  spotykach_core
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# Opens the real editor and runs audio through the real processor (auval does not open the window).
juce_add_console_app(EditorSmokeTest PRODUCT_NAME "EditorSmokeTest")
set_target_properties(EditorSmokeTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(EditorSmokeTest PRIVATE
  tests/editor_smoke_test.cpp
  src/PluginProcessor.cpp
  src/PluginEditor.cpp)
target_compile_definitions(EditorSmokeTest PRIVATE
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_STANDALONE_APPLICATION=1
  JucePlugin_Name="Unofficial Spotykach"
  JucePlugin_VersionString="${PROJECT_VERSION}"
  US_BUILD_ID="${US_BUILD_ID}")
target_link_libraries(EditorSmokeTest PRIVATE
  spotykach_core juce::juce_audio_utils juce::juce_gui_basics
  juce::juce_recommended_config_flags)
