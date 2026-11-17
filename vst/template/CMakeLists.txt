cmake_minimum_required(VERSION 3.22)

project({{TARGET}} VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point at an existing JUCE checkout to avoid re-downloading, e.g.
#   -DJUCE_PATH=../build-vst/_deps/juce-src
if(DEFINED JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/juce" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ---------------------------------------------------------------------------
# Plugin target
# ---------------------------------------------------------------------------
juce_add_plugin({{TARGET}}
    COMPANY_NAME             "{{MANUFACTURER}}"
    PLUGIN_MANUFACTURER_CODE {{MANUFACTURER_CODE}}
    PLUGIN_CODE              {{PLUGIN_CODE}}
    BUNDLE_ID                "{{BUNDLE_ID}}"
    PRODUCT_NAME             "{{PLUGIN_NAME}}"
    FORMATS                  VST3
    VST3_CATEGORIES          Fx
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

target_sources({{TARGET}} PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories({{TARGET}} PRIVATE Source)

target_compile_definitions({{TARGET}} PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_compile_options({{TARGET}} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries({{TARGET}}
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Verification tools (see docs/test-plugin-style-guide.md §7)
# ---------------------------------------------------------------------------
juce_add_console_app({{TARGET}}SelfTest PRODUCT_NAME "{{TARGET}}SelfTest")

target_sources({{TARGET}}SelfTest PRIVATE
    Source/SelfTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories({{TARGET}}SelfTest PRIVATE Source Models)

target_compile_definitions({{TARGET}}SelfTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_compile_options({{TARGET}}SelfTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries({{TARGET}}SelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

juce_add_console_app({{TARGET}}UiShot PRODUCT_NAME "{{TARGET}}UiShot")

target_sources({{TARGET}}UiShot PRIVATE
    Source/UiShot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories({{TARGET}}UiShot PRIVATE Source Models)

target_compile_definitions({{TARGET}}UiShot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_compile_options({{TARGET}}UiShot PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries({{TARGET}}UiShot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
