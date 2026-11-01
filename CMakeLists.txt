cmake_minimum_required(VERSION 3.22)

project(RetroSeq VERSION 0.21.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Statisk C++-runtime pa Windows: RetroSeq.exe behover inget Visual C++ Redistributable
# och kan installeras pa vilken Windows 10/11-dator som helst.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ------------------------------------------------------------------------------
# JUCE: anvand en lokal kopia om JUCE_DIR anges, annars hamtas JUCE 8.0.15
# automatiskt fran GitHub vid forsta cmake-korningen.
# ------------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Sokvag till lokal JUCE-katalog (valfritt)")

if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# ------------------------------------------------------------------------------
# Kosmische: sekvensgeneratorns karna (Source/Kosmische). Inga beroenden till
# JUCE, sa den kan testas fristaende (KosTests) och provlyssnas (kosgen).
# ------------------------------------------------------------------------------
add_library(KosCore STATIC
    Source/Kosmische/KosTheory.cpp
    Source/Kosmische/KosModel.cpp
    Source/Kosmische/KosGenerator.cpp)
target_include_directories(KosCore PUBLIC Source/Kosmische)

option(RETROSEQ_KOS_TOOLS "Bygg Kosmische-testerna (KosTests) och lyssningsverktyget kosgen" ON)
if(RETROSEQ_KOS_TOOLS)
    add_executable(KosTests Tests/Kosmische/KosTests.cpp)
    target_link_libraries(KosTests PRIVATE KosCore)

    add_executable(kosgen Tests/Kosmische/KosGen.cpp)
    target_link_libraries(kosgen PRIVATE KosCore)

    enable_testing()
    add_test(NAME KosTests COMMAND KosTests)
endif()

if(MSVC)
    foreach(kosTarget KosCore KosTests kosgen)
        if(TARGET ${kosTarget})
            target_compile_options(${kosTarget} PRIVATE /utf-8 /W4)
        endif()
    endforeach()
endif()

juce_add_gui_app(RetroSeq
    PRODUCT_NAME "RetroSeq"
    COMPANY_NAME "RetroSeq"
    ICON_BIG     "${CMAKE_CURRENT_SOURCE_DIR}/Resources/RetroSeq.png"
    VERSION      0.21.0)

target_sources(RetroSeq PRIVATE
    Source/Main.cpp
    Source/Song.cpp
    Source/Engine.cpp
    Source/TextScreen.cpp
    Source/MainScreen.cpp
    Source/Transforms.cpp
    Source/StepRecord.cpp
    Source/MidiEdit.cpp
    Source/Kosmische/KosBridge.cpp
    Source/Kosmische/KosMainView.cpp
    Source/Kosmische/KosKitView.cpp
    Source/Kosmische/KosStepEdit.cpp
    Source/Kosmische/KosPatchIO.cpp
    Source/Kosmische/KosWindow.cpp)

target_compile_definitions(RetroSeq PRIVATE
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=0
    JUCE_PLUGINHOST_LV2=0
    JUCE_PLUGINHOST_LADSPA=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # Tredjeparts-plugins (t.ex. PolyBrute Connect) skickar endEdit utan beginEdit;
    # JUCE:s debug-kontroll for detta skulle annars stoppa programmet i Debug.
    JUCE_DISABLE_AUDIOPROCESSOR_BEGIN_END_GESTURE_CHECKING=1
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:RetroSeq,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:RetroSeq,JUCE_VERSION>")

if(WIN32)
    # ASIO: JUCE 8 levererar Steinbergs ASIO-headers, ingen separat SDK behovs
    target_compile_definitions(RetroSeq PRIVATE JUCE_ASIO=1)
endif()

if(MSVC)
    # Kallkoden ar UTF-8 (svenska tecken och ramtecken i strangar)
    target_compile_options(RetroSeq PRIVATE /utf-8)
endif()

target_link_libraries(RetroSeq
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_gui_extra
        KosCore
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Kosmische: test av bryggan mot Engine (skrivning, agarskap, Freeze, loop).
# Kraver inget ljudkort.
# ------------------------------------------------------------------------------
if(RETROSEQ_KOS_TOOLS)
    juce_add_console_app(KosBridgeTests PRODUCT_NAME "KosBridgeTests")
    target_sources(KosBridgeTests PRIVATE
        Tests/Kosmische/KosBridgeTests.cpp
        Source/Engine.cpp
        Source/Song.cpp
        Source/Kosmische/KosBridge.cpp
        Source/Kosmische/KosPatchIO.cpp)
    target_compile_definitions(KosBridgeTests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LV2=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISABLE_AUDIOPROCESSOR_BEGIN_END_GESTURE_CHECKING=1)
    if(WIN32)
        target_compile_definitions(KosBridgeTests PRIVATE JUCE_ASIO=1)
    endif()
    if(MSVC)
        target_compile_options(KosBridgeTests PRIVATE /utf-8)
    endif()
    target_link_libraries(KosBridgeTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_audio_devices
            juce::juce_gui_extra
            KosCore
        PUBLIC
            juce::juce_recommended_config_flags)
    add_test(NAME KosBridgeTests COMMAND KosBridgeTests)
endif()
