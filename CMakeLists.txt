cmake_minimum_required(VERSION 3.22)

# Single source of truth for the version: the VERSION file (also read by the installer scripts).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" OTTOCLONE_VERSION LIMIT_COUNT 1)

if(APPLE)
    # Universal binary (Apple Silicon + Intel). Must be set before project().
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binaries")
endif()

project(Ottoclone VERSION ${OTTOCLONE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # Link the C/C++ runtime statically so that other people's PCs do not need the
    # "Microsoft Visual C++ Redistributable" installed for the plug-in to load.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

option(OTTOBIT_BUILD_TESTS  "Build the offline DSP tests" ON)
option(OTTOBIT_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone, + AU on macOS)" ON)

# The DSP engine is header-only and has no JUCE dependency, so it can be tested on its own.
add_library(ottobit_dsp INTERFACE)
target_include_directories(ottobit_dsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/Source/dsp)

if(OTTOBIT_BUILD_TESTS)
    enable_testing()
    add_executable(ottobit_tests Tests/EngineTests.cpp)
    target_link_libraries(ottobit_tests PRIVATE ottobit_dsp)
    if(MSVC)
        target_compile_options(ottobit_tests PRIVATE /W4 /fp:precise)
    else()
        target_compile_options(ottobit_tests PRIVATE -Wall -Wextra)
    endif()
    add_test(NAME dsp COMMAND ottobit_tests)
endif()

if(OTTOBIT_BUILD_PLUGIN)
    add_subdirectory(third_party/JUCE)

    set(OTTOCLONE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND OTTOCLONE_FORMATS AU)
    endif()

    juce_add_plugin(Ottoclone
        COMPANY_NAME                "Ottoclone"
        BUNDLE_ID                   com.ottoclone.Ottoclone
        PLUGIN_MANUFACTURER_CODE    Ottc
        PLUGIN_CODE                 Otcl
        PRODUCT_NAME                "Ottoclone"
        VERSION                     ${OTTOCLONE_VERSION}
        FORMATS                     ${OTTOCLONE_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        VST3_CATEGORIES             Fx Distortion
        AU_MAIN_TYPE                kAudioUnitType_Effect
        # macOS only: lets the Standalone app receive audio input.
        MICROPHONE_PERMISSION_ENABLED TRUE
        MICROPHONE_PERMISSION_TEXT  "Ottoclone processes the audio coming from your audio interface."
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(Ottoclone PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(Ottoclone PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(Ottoclone
        PRIVATE
            ottobit_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

    # Optional: a console host that loads the built VST3, runs audio through it, renders the
    # editor to a PNG and writes demo WAVs.   cmake -DOTTOBIT_BUILD_SMOKETEST=ON ...
    option(OTTOBIT_BUILD_SMOKETEST "Build the VST3 load/render smoke test" OFF)
    if(OTTOBIT_BUILD_SMOKETEST)
        juce_add_console_app(OttocloneSmokeTest PRODUCT_NAME "OttocloneSmokeTest")
        target_sources(OttocloneSmokeTest PRIVATE Tools/PluginSmokeTest.cpp)
        target_compile_definitions(OttocloneSmokeTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(OttocloneSmokeTest PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
            juce::juce_recommended_config_flags)
        add_dependencies(OttocloneSmokeTest Ottoclone_VST3)

        # Renders the editor (both layers) straight to PNG, no host or window needed.
        juce_add_console_app(OttocloneEditorSnapshot PRODUCT_NAME "OttocloneEditorSnapshot")
        target_sources(OttocloneEditorSnapshot PRIVATE
            Tools/EditorSnapshot.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp)
        target_include_directories(OttocloneEditorSnapshot PRIVATE Source)
        target_compile_definitions(OttocloneEditorSnapshot PRIVATE
            JucePlugin_Name="Ottoclone"
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(OttocloneEditorSnapshot PRIVATE
            ottobit_dsp
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
    endif()
endif()
