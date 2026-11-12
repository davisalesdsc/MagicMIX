cmake_minimum_required(VERSION 3.22)

# ---------------------------------------------------------------------------
# MagicMIX - plugin VST3 / AU / Standalone
# Motor de áudio próprio em C++ (Source/Engine, sem JUCE) + JUCE na interface,
# nos formatos de plugin e na chamada ao serviço de IA.
# ---------------------------------------------------------------------------
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSAO" MM_VERSAO LIMIT_COUNT 1)
string(STRIP "${MM_VERSAO}" MM_VERSAO)

if(APPLE)
    # Um binário universal: Apple Silicon e Intel no mesmo plugin.
    if(NOT DEFINED MM_OSX_ARCH)
        set(MM_OSX_ARCH "arm64;x86_64")
    endif()
    set(CMAKE_OSX_ARCHITECTURES "${MM_OSX_ARCH}" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

project(MagicMIX VERSION ${MM_VERSAO} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    add_compile_options(/utf-8)
endif()

# Windows: runtime do Visual C++ embutido (/MT). Sem isso o plugin exige o
# "Visual C++ Redistributable", e num PC sem ele o host não lista o plugin.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "" FORCE)

include(FetchContent)

# ---- JUCE ------------------------------------------------------------------
if(DEFINED MM_JUCE_DIR AND EXISTS "${MM_JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${MM_JUCE_DIR}" juce)
else()
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ---- motor de áudio (C++ puro) ---------------------------------------------
add_library(MagicMixEngine STATIC
    Source/Engine/Catalog.cpp
    Source/Engine/Validate.cpp
    Source/Engine/Modules.cpp
    Source/Engine/Chain.cpp
    Source/Engine/Analyzer.cpp)
target_include_directories(MagicMixEngine PUBLIC Source/Engine)
if(NOT MSVC)
    target_compile_options(MagicMixEngine PRIVATE -Wall -Wextra)
endif()

# O prompt de sistema vai embutido no binário.
juce_add_binary_data(MagicMixData SOURCES Source/Ai/PromptSistema.txt)

# ---- identidade ------------------------------------------------------------
set(MM_COMPANY     "Davi Sales")
set(MM_BUNDLE_BASE "com.davisales.magicmix")

set(MM_DEFS
    JUCE_WEB_BROWSER=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    MM_VERSAO="${MM_VERSAO}")

# No Linux a chamada ao serviço de IA usa libcurl (no macOS/Windows usa a API do sistema).
set(MM_NEEDS_CURL FALSE)
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    set(MM_NEEDS_CURL TRUE)
    list(APPEND MM_DEFS JUCE_USE_CURL=1)
endif()

option(MM_COPY_PLUGIN "Copia o plugin para a pasta de plugins do sistema após compilar" OFF)

set(MM_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND MM_FORMATS AU)
endif()

set(MM_PLUGIN_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Ai/SpecJson.cpp
    Source/Ai/Prompt.cpp
    Source/Ai/IntentClient.cpp)

juce_add_plugin(MagicMIX
    COMPANY_NAME                "${MM_COMPANY}"
    BUNDLE_ID                   "${MM_BUNDLE_BASE}.plugin"
    PLUGIN_MANUFACTURER_CODE    DvSl
    PLUGIN_CODE                 MgMx
    FORMATS                     ${MM_FORMATS}
    PRODUCT_NAME                "MagicMIX"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_CURL                  ${MM_NEEDS_CURL}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT  "O MagicMIX usa a entrada de áudio para processar o som."
    COPY_PLUGIN_AFTER_BUILD     ${MM_COPY_PLUGIN}
    VST3_CATEGORIES             Fx Dynamics EQ
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(MagicMIX PRIVATE ${MM_PLUGIN_SOURCES})
target_include_directories(MagicMIX PRIVATE Source)
target_compile_definitions(MagicMIX PUBLIC ${MM_DEFS})
target_link_libraries(MagicMIX
    PRIVATE
        MagicMixEngine
        MagicMixData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
if(NOT MSVC)
    target_link_libraries(MagicMIX PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- testes (console) --------------------------------------------------------
option(MM_BUILD_TESTS "Compila os testes de console" OFF)
if(MM_BUILD_TESTS)
    # motor: C++ puro, sem JUCE
    add_executable(EngineTests tests/EngineTests.cpp)
    target_link_libraries(EngineTests PRIVATE MagicMixEngine)

    # camada do plugin: JSON, pedido ao serviço de IA e o processador rodando áudio
    juce_add_console_app(PluginTests PRODUCT_NAME "PluginTests")
    target_sources(PluginTests PRIVATE tests/PluginTests.cpp ${MM_PLUGIN_SOURCES})
    target_include_directories(PluginTests PRIVATE Source)
    target_compile_definitions(PluginTests PRIVATE ${MM_DEFS} JUCE_STANDALONE_APPLICATION=1
        JucePlugin_Name="MagicMIX" JucePlugin_Build_Standalone=1 JUCE_MODAL_LOOPS_PERMITTED=1
        MM_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(PluginTests PRIVATE MagicMixEngine MagicMixData juce::juce_audio_utils
        PUBLIC juce::juce_recommended_config_flags)
    if(MM_NEEDS_CURL)
        target_link_libraries(PluginTests PRIVATE CURL::libcurl)
    endif()

    # host de teste: carrega o VST3/AU compilado como um DAW
    juce_add_console_app(HostTest PRODUCT_NAME "HostTest")
    target_sources(HostTest PRIVATE tests/HostTest.cpp)
    target_compile_definitions(HostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
        $<$<PLATFORM_ID:Darwin>:JUCE_PLUGINHOST_AU=1>)
    target_link_libraries(HostTest PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
        PUBLIC juce::juce_recommended_config_flags)
    if(APPLE)
        target_link_libraries(HostTest PRIVATE "-framework CoreAudioKit" "-framework AudioUnit")
    endif()
endif()
