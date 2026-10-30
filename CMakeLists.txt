cmake_minimum_required(VERSION 3.22)

# Single source of truth for the version: the VERSION file (also read by the installer scripts).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BITVIBE_VERSION LIMIT_COUNT 1)

if(APPLE)
    # Universal binary (Apple Silicon + Intel). Must be set before project().
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binaries")
endif()

project(BitVibe VERSION ${BITVIBE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Link the C/C++ runtime statically so that other people's PCs do not need the
    # "Microsoft Visual C++ Redistributable" installed for the plug-in to load.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(BITVIBE_BUILD_TESTS  "Build the offline DSP tests" ON)
option(BITVIBE_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone, + AU on macOS)" ON)

# The DSP engine is header-only and has no JUCE dependency, so it can be tested on its own.
add_library(bitvibe_dsp INTERFACE)
target_include_directories(bitvibe_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp)

if(BITVIBE_BUILD_TESTS)
    enable_testing()
    add_executable(bitvibe_tests Tests/EngineTests.cpp)
    target_link_libraries(bitvibe_tests PRIVATE bitvibe_dsp)
    if(MSVC)
        target_compile_options(bitvibe_tests PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(bitvibe_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME dsp COMMAND bitvibe_tests)

    # Development tool: scores the built-in drum analysis against annotated recordings.
    add_executable(bitvibe_drum_eval Tools/DrumEval.cpp)
    target_link_libraries(bitvibe_drum_eval PRIVATE bitvibe_dsp)
    if(MSVC)
        target_compile_options(bitvibe_drum_eval PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(bitvibe_drum_eval PRIVATE -Wall -Wextra)
    endif()
endif()

if(BITVIBE_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)

    set(BITVIBE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND BITVIBE_FORMATS AU)
    endif()

    juce_add_plugin(BitVibe
        COMPANY_NAME                "BitVibe"
        BUNDLE_ID                   com.bitvibe.BitVibe
        PLUGIN_MANUFACTURER_CODE    Bvbe
        PLUGIN_CODE                 Bvib
        PRODUCT_NAME                "BitVibe"
        VERSION                     ${BITVIBE_VERSION}
        FORMATS                     ${BITVIBE_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        VST3_CATEGORIES             Fx Distortion
        AU_MAIN_TYPE                kAudioUnitType_Effect
        # macOS only: lets the Standalone app receive audio input.
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT  "BitVibe processes the audio coming from your audio interface."
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(BitVibe PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/DrumPage.cpp
        Source/PatternPanel.cpp
        Source/SamplePanel.cpp
        Source/EqPanel.cpp
        Source/SampleBank.cpp
        Source/DrumBeat.cpp)

    # The DRUMS tab lists the audio files in this folder (local builds only; installed copies use
    # Documents/BitVibe/Beats).
    set(BITVIBE_BEATS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/beats")
    # The sample blend lists the sounds in this folder (local builds only; installed copies use Documents/BitVibe/Sounds).
    set(BITVIBE_SOUNDS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/GoodSounds")

    target_compile_definitions(BitVibe PUBLIC
        BITVIBE_BEATS_DIR="${BITVIBE_BEATS_DIR}"
        BITVIBE_SOUNDS_DIR="${BITVIBE_SOUNDS_DIR}"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(BitVibe
        PRIVATE
            bitvibe_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Optional: a console host that loads the built VST3, runs audio through it, renders the
    # editor to a PNG and writes demo WAVs.   cmake -DBITVIBE_BUILD_SMOKETEST=ON ...
    option(BITVIBE_BUILD_SMOKETEST "Build the VST3 load/render smoke test" OFF)
    if(BITVIBE_BUILD_SMOKETEST)
        juce_add_console_app(BitVibeSmokeTest PRODUCT_NAME "BitVibeSmokeTest")
        target_sources(BitVibeSmokeTest PRIVATE Tools/PluginSmokeTest.cpp)
        target_compile_definitions(BitVibeSmokeTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(BitVibeSmokeTest PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
            juce::juce_recommended_config_flags)
        add_dependencies(BitVibeSmokeTest BitVibe_VST3)

        # Renders the editor (both layers) straight to PNG, no host or window needed.
        juce_add_console_app(BitVibeEditorSnapshot PRODUCT_NAME "BitVibeEditorSnapshot")
        target_sources(BitVibeEditorSnapshot PRIVATE
            Tools/EditorSnapshot.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp
            Source/DrumPage.cpp
        Source/PatternPanel.cpp
        Source/SamplePanel.cpp
        Source/EqPanel.cpp
        Source/SampleBank.cpp
            Source/DrumBeat.cpp)
        target_include_directories(BitVibeEditorSnapshot PRIVATE Source)
        target_compile_definitions(BitVibeEditorSnapshot PRIVATE
            BITVIBE_BEATS_DIR="${BITVIBE_BEATS_DIR}"
            BITVIBE_SOUNDS_DIR="${BITVIBE_SOUNDS_DIR}"
            JucePlugin_Name="BitVibe"
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(BitVibeEditorSnapshot PRIVATE
            bitvibe_dsp
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
    endif()
endif()
