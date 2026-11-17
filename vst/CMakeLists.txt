cmake_minimum_required(VERSION 3.22)

project(NamForgeKrakenDemo VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# Pinned JUCE. 8.0.4 declares cmake_minimum_required(3.22), which CMake 4.x accepts.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(JUCE)

juce_add_plugin(NamForgeKrakenDemo
    COMPANY_NAME             "Cosmic Crucible Development"
    PLUGIN_MANUFACTURER_CODE CCDV
    PLUGIN_CODE              NKDM
    BUNDLE_ID                "com.cosmiccrucible.namforge.krakendemo"
    PRODUCT_NAME             "NamForge Kraken Demo"
    FORMATS                  VST3
    VST3_CATEGORIES          Fx
    IS_SYNTH                 FALSE
    NEEDS_MIDI_INPUT         FALSE
    NEEDS_MIDI_OUTPUT        FALSE
    IS_MIDI_EFFECT           FALSE
    COPY_PLUGIN_AFTER_BUILD  FALSE
)

target_sources(NamForgeKrakenDemo PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# Single canonical model headers. Do NOT add packs/*/src here: the two packs ship
# different NamA2Core.h copies defining the same symbols (C2011 redefinition).
target_include_directories(NamForgeKrakenDemo PRIVATE
    Source/Models
)

target_compile_definitions(NamForgeKrakenDemo PRIVATE
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

# The inference core selects its SIMD path at compile time via __AVX2__.
target_compile_options(NamForgeKrakenDemo PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries(NamForgeKrakenDemo
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Offline parameter self-test. Catches "knob moves but nothing happens" bugs
# without needing to load the plugin in a DAW.
juce_add_console_app(NamForgeSelfTest PRODUCT_NAME "NamForgeSelfTest")

target_sources(NamForgeSelfTest PRIVATE
    Source/SelfTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(NamForgeSelfTest PRIVATE
    Source/Models
    Source
)

target_compile_definitions(NamForgeSelfTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_compile_options(NamForgeSelfTest PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries(NamForgeSelfTest
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Headless editor renderer, for checking the UI without launching a DAW.
juce_add_console_app(NamForgeUiShot PRODUCT_NAME "NamForgeUiShot")

target_sources(NamForgeUiShot PRIVATE
    Source/UiShot.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_include_directories(NamForgeUiShot PRIVATE
    Source/Models
    Source
)

target_compile_definitions(NamForgeUiShot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=1
)

target_compile_options(NamForgeUiShot PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
    $<$<CXX_COMPILER_ID:MSVC>:/fp:fast>
)

target_link_libraries(NamForgeUiShot
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)

# Large-format knob renderer for tuning the LookAndFeel against reference art.
juce_add_console_app(NamForgeKnobShot PRODUCT_NAME "NamForgeKnobShot")

target_sources(NamForgeKnobShot PRIVATE
    Source/KnobShot.cpp
)

target_compile_definitions(NamForgeKnobShot PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)

target_link_libraries(NamForgeKnobShot
    PRIVATE
        juce::juce_gui_basics
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
