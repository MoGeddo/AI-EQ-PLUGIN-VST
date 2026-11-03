cmake_minimum_required(VERSION 3.22)
project(AEQRestore VERSION 1.2.4 LANGUAGES CXX)

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

# The UI font is shipped inside the binary.  Asking Windows for a typeface is what
# produced the tofu in 1.2.0: on a machine without the Arabic language pack the
# lookup returns a font with no Arabic glyphs.  Cairo is SIL OFL 1.1, so it can be
# redistributed; the licence travels with it in Source/assets.
juce_add_binary_data(AEQRestoreFonts SOURCES
    Source/assets/Cairo-Regular.ttf
    Source/assets/Cairo-SemiBold.ttf)

target_sources(AEQRestore PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/ArabicText.cpp)

target_include_directories(AEQRestore PRIVATE Source)

# The interface is written in Arabic, so the source files contain non-ASCII
# literals.  MSVC guesses the source encoding from the machine's code page unless
# it is told otherwise, and this machine is not always in UTF-8 mode: the literals
# then arrive in the binary as something that is not Arabic, and the window fills
# with .notdef boxes while the Latin text in the same string renders fine.  That is
# exactly the failure the user photographed.  /utf-8 says: the file is UTF-8 and the
# narrow literals are UTF-8.  Belt and braces: the strings themselves are written as
# \uXXXX escapes in UiStrings.h, which no code page can change.
if (MSVC)
    target_compile_options(AEQRestore PRIVATE /utf-8)
else()
    target_compile_options(AEQRestore PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

target_compile_definitions(AEQRestore PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_link_libraries(AEQRestore PRIVATE
    AEQRestoreFonts
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ThreadSanitizer when the compiler supports it, a plain build otherwise.  Guarded
# so the Windows release build is untouched.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
check_cxx_source_compiles("int main(){return 0;}" AEQ_HAVE_TSAN)
unset(CMAKE_REQUIRED_FLAGS)
add_library(aeq_tsan INTERFACE)
if(AEQ_HAVE_TSAN AND NOT MSVC)
    target_compile_options(aeq_tsan INTERFACE -fsanitize=thread -g -O1)
    target_link_options(aeq_tsan INTERFACE -fsanitize=thread)
    message(STATUS "AEQ: ThreadSanitizer enabled for aeq_threads")
endif()

# ---- a real screenshot, from the real editor ------------------------------------
# A console app that builds the plugin, feeds it a synthetic take until the
# analysis settles, constructs the editor and snapshots the component tree.  It is
# built on Windows in CI on purpose: the font stack that produced the tofu in 1.2.0
# is the Windows one, so a screenshot rendered anywhere else proves nothing.
juce_add_console_app(aeq_shot PRODUCT_NAME "aeq_shot")
target_sources(aeq_shot PRIVATE
    Tests/ui_shot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/ui/ArabicText.cpp)
target_include_directories(aeq_shot PRIVATE Source)
if (MSVC)
    target_compile_options(aeq_shot PRIVATE /utf-8)
endif()
target_compile_definitions(aeq_shot PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1 JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    # Direct2D stays ON, exactly as in the plugin.  Turning it off was a mistake
    # worth recording: the software renderer on Windows draws glyphs through GDI,
    # GDI cannot use a font that lives in memory, so it substitutes an installed
    # face - and the window came out as a wall of boxes even though the embedded
    # Cairo had loaded.  The screenshot has to use the same renderer as the
    # product or it is measuring the wrong thing.
    JUCE_DIRECT2D=1)
target_link_libraries(aeq_shot PRIVATE
    AEQRestoreFonts
    juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
    juce::juce_gui_basics juce::juce_gui_extra
    juce::juce_recommended_config_flags)

# ---- the DSP core, without JUCE: same code the gates run ----
add_executable(aeq_cli cli/main.cpp)
target_include_directories(aeq_cli PRIVATE Source)
set_target_properties(aeq_cli PROPERTIES CXX_STANDARD 17)

enable_testing()
add_executable(aeq_tests Tests/test_core.cpp)
target_include_directories(aeq_tests PRIVATE Source)
set_target_properties(aeq_tests PROPERTIES CXX_STANDARD 17)
add_test(NAME acceptance_gates COMMAND aeq_tests)

# The audio thread and the editor thread run at the same time in every DAW; this
# proves they do not share anything unprotected.  Built with TSAN where the
# toolchain has it, because a plain run only catches races that happen to fire.
add_executable(aeq_threads Tests/test_threads.cpp)
target_include_directories(aeq_threads PRIVATE Source)
target_link_libraries(aeq_threads PRIVATE aeq_tsan)
set_target_properties(aeq_threads PROPERTIES CXX_STANDARD 17)
add_test(NAME thread_contract COMMAND aeq_threads)

# AddressSanitizer for the hostile-host test.  That test asserts on behaviour, but
# its real job is to turn an out-of-bounds write on the audio thread into a failure
# on this machine rather than a crash in somebody's DAW - which is exactly how the
# 1.2.2 FL Studio exit was found (heap-buffer-overflow WRITE of size 4,
# RestoreCore.h:108).  Detected rather than assumed, so a toolchain without it still
# builds the gate; it is only weaker, never absent.
# MSVC is deliberately left alone: cl accepts /fsanitize=address and CMake's
# try_compile reports success, but the target links through link.exe where the flag
# is rejected with LNK4044 and the resulting binary exits with no output - which is
# how the first 1.2.3 build failed its own gate.  A gate that cannot be trusted is
# worse than one that is merely weaker, so Windows gets the same test built plainly
# (it still catches a deadlock and a dropped block) and the sanitizer runs on the
# Linux gate, where it is the tool that actually found the overflow.
if (NOT MSVC)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address,undefined")
    check_cxx_source_compiles("int main(){return 0;}" AEQ_HAVE_ASAN)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
endif()
add_library(aeq_asan INTERFACE)
if (AEQ_HAVE_ASAN AND NOT MSVC)
    target_compile_options(aeq_asan INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer -g -O1)
    target_link_options(aeq_asan INTERFACE -fsanitize=address,undefined)
    message(STATUS "AEQ: AddressSanitizer enabled for aeq_bounds")
else()
    message(STATUS "AEQ: aeq_bounds built without AddressSanitizer (behavioural checks only)")
endif()

# A host may hand over a block bigger than the one it prepared with, change the
# size on every callback, run mono, or call prepareToPlay() again mid-playback.
add_executable(aeq_bounds Tests/test_bounds.cpp)
target_include_directories(aeq_bounds PRIVATE Source)
target_link_libraries(aeq_bounds PRIVATE aeq_asan)
set_target_properties(aeq_bounds PROPERTIES CXX_STANDARD 17)
add_test(NAME hostile_host COMMAND aeq_bounds)
