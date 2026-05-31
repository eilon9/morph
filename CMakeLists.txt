cmake_minimum_required(VERSION 3.22)
project(Morph VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(EXISTS "C:/JUCE/CMakeLists.txt")
    add_subdirectory("C:/JUCE" JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.3
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# -------------------------------------------------------------------
# Morph plugin — the crossfader that reads from shared memory
# -------------------------------------------------------------------
juce_add_plugin(Morph
    PLUGIN_MANUFACTURER_CODE Mrph
    PLUGIN_CODE Mxfd
    FORMATS VST3 Standalone
    PRODUCT_NAME "Morph"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(Morph)

target_sources(Morph PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/LPCMorphEngine.cpp
)

target_compile_definitions(Morph PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Morph PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# -------------------------------------------------------------------
# MorphListener plugin — captures audio and writes to shared memory
# -------------------------------------------------------------------
juce_add_plugin(MorphListener
    PLUGIN_MANUFACTURER_CODE Mrph
    PLUGIN_CODE Lstn
    FORMATS VST3
    PRODUCT_NAME "MorphListener"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

juce_generate_juce_header(MorphListener)

target_sources(MorphListener PRIVATE
    Listener/ListenerProcessor.cpp
    Listener/ListenerEditor.cpp
)

target_compile_definitions(MorphListener PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(MorphListener PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)