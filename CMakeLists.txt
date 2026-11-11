cmake_minimum_required(VERSION 3.22)
project(Mindroll VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(MO_FORMATS AU VST3 Standalone)
else()
    set(MO_FORMATS VST3 Standalone)
endif()

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Vocal stutter build: repite un trozo de vocal cada vez más rápido hasta el drop. Va en la pista de la vocal.
juce_add_plugin(Mindroll
    COMPANY_NAME "Dj Nane"
    BUNDLE_ID com.djnane.mindroll
    PLUGIN_MANUFACTURER_CODE DjNa
    PLUGIN_CODE MnRl
    FORMATS ${MO_FORMATS}
    NEEDS_WEB_BROWSER TRUE
    PRODUCT_NAME "MINDROLL"
    PLUGIN_NAME "MINDROLL"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx)

# ui/index.html se genera con tools/build_ui.py (skin común + fuentes + marco embebidos)
juce_add_binary_data(MoData SOURCES ui/index.html ui/frame.jpg common/Nunito-ExtraBold.ttf)

target_sources(Mindroll PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/RollEngine.cpp
    common/DragBar.cpp)

target_include_directories(Mindroll PRIVATE common)

target_compile_definitions(Mindroll PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Mindroll
    PRIVATE
        MoData
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Prueba del motor sin interfaz (solo Linux/CI): cmake -DMO_TESTS=ON
option(MO_TESTS "Build engine tests" OFF)
if(MO_TESTS)
    juce_add_console_app(MoEngineTest PRODUCT_NAME "MoEngineTest")
    target_sources(MoEngineTest PRIVATE tests/engine_test.cpp Source/RollEngine.cpp)
    target_compile_definitions(MoEngineTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MoEngineTest PRIVATE juce::juce_audio_basics juce::juce_events juce::juce_recommended_config_flags)

    juce_add_console_app(MoRender PRODUCT_NAME "MoRender")
    target_sources(MoRender PRIVATE tests/render.cpp Source/RollEngine.cpp)
    target_compile_definitions(MoRender PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(MoRender PRIVATE juce::juce_audio_formats juce::juce_events juce::juce_recommended_config_flags)
endif()
if(MO_TESTS)
    add_executable(MoProcessorTest tests/processor_test.cpp)
    target_link_libraries(MoProcessorTest PRIVATE Mindroll)
    target_include_directories(MoProcessorTest PRIVATE $<TARGET_PROPERTY:Mindroll,INCLUDE_DIRECTORIES>)
    target_compile_definitions(MoProcessorTest PRIVATE $<TARGET_PROPERTY:Mindroll,COMPILE_DEFINITIONS>)
endif()
