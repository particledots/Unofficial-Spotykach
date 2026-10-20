cmake_minimum_required(VERSION 3.22)

# Universal binary + macOS 12 floor. These must be set BEFORE project().
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

# The version shown on the About screen comes from here.
project(UnofficialSpotykach VERSION 0.1.0)

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
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)
