cmake_minimum_required(VERSION 3.22)
project(CrayAutoTune VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Runtime de MSVC enlazado estaticamente (/MT): el .vst3 NO exige instalar el VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6          # fija la version; verifica que exista y actualiza si quieres
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(CrayAutoTune
    COMPANY_NAME               "Cray LDR"
    PRODUCT_NAME               "CRAY AutoTune LDR"
    PLUGIN_MANUFACTURER_CODE   CrLd
    PLUGIN_CODE                CAtl
    FORMATS                    VST3
    VST3_CATEGORIES            Fx "Pitch Shift"
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           FALSE
    NEEDS_MIDI_OUTPUT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Copiar a C:\Program Files\Common Files\VST3 exige administrador: se hace a mano (ver README).
    COPY_PLUGIN_AFTER_BUILD    FALSE)

target_sources(CrayAutoTune PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/CrayAutoTuneDSP.h)

target_compile_definitions(CrayAutoTune PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(CrayAutoTune
    PRIVATE juce::juce_audio_utils
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

if(MSVC)
    target_compile_options(CrayAutoTune PRIVATE /W4 /permissive- /Zc:__cplusplus)
endif()

# Prueba offline del DSP (sin JUCE):  cmake --build build --target dsp_test --config Release
add_executable(dsp_test Tests/dsp_test.cpp)
if(MSVC)
    target_compile_options(dsp_test PRIVATE /O2 /W3)
endif()
