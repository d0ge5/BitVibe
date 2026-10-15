cmake_minimum_required(VERSION 3.22)

project(OttobitJrReplica VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OTTOBIT_BUILD_TESTS  "Build the offline DSP tests" ON)
option(OTTOBIT_BUILD_PLUGIN "Build the JUCE plugin (VST3 + Standalone)" ON)

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

    juce_add_plugin(OttobitJrReplica
        COMPANY_NAME                "Unofficial"
        PLUGIN_MANUFACTURER_CODE    Dnch
        PLUGIN_CODE                 Objr
        PRODUCT_NAME                "Ottobit Jr Replica"
        FORMATS                     VST3 Standalone
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        VST3_CATEGORIES             Fx Distortion
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(OttobitJrReplica PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp)

    target_compile_definitions(OttobitJrReplica PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(OttobitJrReplica
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
        juce_add_console_app(OttobitSmokeTest PRODUCT_NAME "OttobitSmokeTest")
        target_sources(OttobitSmokeTest PRIVATE Tools/PluginSmokeTest.cpp)
        target_compile_definitions(OttobitSmokeTest PRIVATE
            JUCE_PLUGINHOST_VST3=1
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(OttobitSmokeTest PRIVATE
            juce::juce_audio_processors
            juce::juce_audio_formats
            juce::juce_gui_basics
            juce::juce_recommended_config_flags)
        add_dependencies(OttobitSmokeTest OttobitJrReplica_VST3)

        # Renders the editor (both layers) straight to PNG, no host or window needed.
        juce_add_console_app(OttobitEditorSnapshot PRODUCT_NAME "OttobitEditorSnapshot")
        target_sources(OttobitEditorSnapshot PRIVATE
            Tools/EditorSnapshot.cpp
            Source/PluginProcessor.cpp
            Source/PluginEditor.cpp)
        target_include_directories(OttobitEditorSnapshot PRIVATE Source)
        target_compile_definitions(OttobitEditorSnapshot PRIVATE
            JucePlugin_Name="Ottobit Jr Replica"
            JUCE_USE_CURL=0
            JUCE_WEB_BROWSER=0)
        target_link_libraries(OttobitEditorSnapshot PRIVATE
            ottobit_dsp
            juce::juce_audio_utils
            juce::juce_recommended_config_flags)
    endif()
endif()
