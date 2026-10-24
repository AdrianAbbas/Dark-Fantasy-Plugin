cmake_minimum_required(VERSION 3.22)
# macOS defaults: one universal binary that loads on Apple Silicon and Intel Macs
# back to 10.15. These must be set before project(); override on the command line.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Target architectures")
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()
project(VEIL VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(VEIL_BUILD_TESTS "Build offline DSP and processor regression tests" ON)
set(JUCE_PATH "" CACHE PATH "Existing JUCE 8.0.16 source (otherwise fetched)")
if(JUCE_PATH)
    add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG ea77444750de45e036e0279bb0291144bf8e3688 # JUCE 8.0.16
        GIT_SHALLOW FALSE)
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
    # Lets sandboxed hosts (GarageBand) load VEIL in-process. VEIL must still work
    # out-of-process, which the AU host harness checks.
    AU_SANDBOX_SAFE TRUE
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
