cmake_minimum_required(VERSION 3.22)
project(AEQRestore VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")

# JUCE 8 - fetched at configure time, no vendoring needed.  If -DJUCE_PATH=<dir>
# points at a JUCE checkout on disk it is used instead, which is what an offline
# or repeatable build needs (the first FetchContent run needs github access).
if(JUCE_PATH)
    message(STATUS "AEQ: using JUCE from ${JUCE_PATH}")
    add_subdirectory(${JUCE_PATH} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(AEQRestore
    COMPANY_NAME              "AEQ"
    PLUGIN_MANUFACTURER_CODE  "Aeqx"
    PLUGIN_CODE               "AeqR"
    FORMATS                   VST3 Standalone
    PRODUCT_NAME              "AEQ Restore"
    IS_SYNTH                  FALSE
    NEEDS_MIDI_INPUT          FALSE
    NEEDS_MIDI_OUTPUT         FALSE
    IS_MIDI_EFFECT            FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD   FALSE
    VST3_CATEGORIES           "Fx" "EQ")

target_sources(AEQRestore PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_include_directories(AEQRestore PRIVATE Source)

target_compile_definitions(AEQRestore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(AEQRestore PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---- the DSP core, without JUCE: same code the gates run ----
add_executable(aeq_cli cli/main.cpp)
target_include_directories(aeq_cli PRIVATE Source)
set_target_properties(aeq_cli PROPERTIES CXX_STANDARD 17)

enable_testing()
add_executable(aeq_tests Tests/test_core.cpp)
target_include_directories(aeq_tests PRIVATE Source)
set_target_properties(aeq_tests PROPERTIES CXX_STANDARD 17)
add_test(NAME acceptance_gates COMMAND aeq_tests)
