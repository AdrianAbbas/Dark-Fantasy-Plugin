cmake_minimum_required(VERSION 3.22)
project(VEIL VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(VEIL_BUILD_TESTS "Build offline DSP and processor regression tests" ON)
set(JUCE_PATH "" CACHE PATH "Existing JUCE 8.0.6 source (otherwise fetched)")
if(JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 51a8a6d7aeae7326956d747737ccf1575e61e209 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(JUCE)
endif()
set(VEIL_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND VEIL_FORMATS AU)
endif()
juce_add_plugin(VEIL
    COMPANY_NAME "Veil Audio"
    BUNDLE_ID "audio.veil.processor"
    PLUGIN_MANUFACTURER_CODE Veil
    PLUGIN_CODE Vel1
    PRODUCT_NAME "VEIL"
    DESCRIPTION "Dark Fantasy Audio Processor"
    FORMATS ${VEIL_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Fx Reverb Delay
    AU_MAIN_TYPE kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD FALSE)
target_sources(VEIL PRIVATE
    Source/PluginProcessor.cpp Source/PluginEditor.cpp
    Source/RitualEngine.cpp Source/PresetManager.cpp
    Source/DSP/BasicModules.cpp Source/DSP/SpatialModules.cpp
    Source/UI/VeilLookAndFeel.cpp Source/UI/ParameterControls.cpp Source/UI/RitualSigil.cpp)
target_include_directories(VEIL PUBLIC Source)
target_compile_definitions(VEIL PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_link_libraries(VEIL PRIVATE juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
if(VEIL_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(VeilTests PRODUCT_NAME "VEIL Tests")
    # The linked plugin target exports the same setting using its format macro.
    get_target_property(veil_test_definitions VeilTests COMPILE_DEFINITIONS)
    list(REMOVE_ITEM veil_test_definitions JUCE_STANDALONE_APPLICATION=1)
    set_target_properties(VeilTests PROPERTIES COMPILE_DEFINITIONS "${veil_test_definitions}")
    target_sources(VeilTests PRIVATE Tests/VeilTests.cpp)
    target_link_libraries(VeilTests PRIVATE VEIL juce::juce_audio_utils juce::juce_dsp)
    target_compile_definitions(VeilTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
    add_test(NAME veil_regression COMMAND VeilTests)
endif()
