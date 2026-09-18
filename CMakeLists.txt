cmake_minimum_required(VERSION 3.22)
project(JustifiedLevelControl VERSION 1.0.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Target Apple Silicon and Intel processors for macOS Universal Binary
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for Mac" FORCE)
endif()

# Fetch JUCE automatically via FetchContent
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.0
)
FetchContent_MakeAvailable(JUCE)

# Define the Audio Plugin Target
juce_add_plugin(JustifiedLevelControl
    COMPANY_NAME "BeenWell"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Bwel
    PLUGIN_CODE Jlc1
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Justified Level Control"
    VERSION "1.0.1"
)

# Add Binary Resources
juce_add_binary_data(JustifiedLevelControl_resources
    HEADER_NAME "BinaryData.h"
    NAMESPACE BinaryData
    SOURCES
        "Resources/JLC.svg"
)

# Source files
target_sources(JustifiedLevelControl
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
)

# Link required JUCE modules
target_link_libraries(JustifiedLevelControl
    PRIVATE
        JustifiedLevelControl_resources
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# Preprocessor defines
target_compile_definitions(JustifiedLevelControl
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)
