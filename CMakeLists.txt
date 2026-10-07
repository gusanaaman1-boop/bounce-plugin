cmake_minimum_required(VERSION 3.22)

# Universal macOS binary - Cubase under Rosetta runs x86_64. Must be set before project().
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    # Without this the binary's minimum macOS is the build machine's SDK and it refuses to
    # load on any older Mac. 10.13 is the floor for the WebView's custom-scheme provider.
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)
endif()

project(Bounce VERSION 1.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 9.0.0 - the shared ~/JUCE checkout every plug-in in this workspace uses. Override with
# -DBOUNCE_JUCE_DIR=... ; a CI checkout goes into ./JUCE.
if(NOT BOUNCE_JUCE_DIR)
    if(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/JUCE/CMakeLists.txt")
        set(BOUNCE_JUCE_DIR "$ENV{HOME}/JUCE")
    elseif(DEFINED ENV{USERPROFILE} AND EXISTS "$ENV{USERPROFILE}/JUCE/CMakeLists.txt")
        set(BOUNCE_JUCE_DIR "$ENV{USERPROFILE}/JUCE")
    else()
        set(BOUNCE_JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE")
    endif()
endif()
add_subdirectory(${BOUNCE_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)

if(APPLE)
    set(BOUNCE_FORMATS VST3 AU Standalone)
else()
    set(BOUNCE_FORMATS VST3 Standalone)
endif()

option(BOUNCE_COPY_AFTER_BUILD "Copy built plug-ins into the user plug-in folders" ON)
option(BOUNCE_WERROR "Treat warnings in BOUNCE's own sources as errors" OFF)
option(BOUNCE_SANITIZERS "Build the test suite with ASan + UBSan" OFF)

if(MSVC)
    set(BOUNCE_WARNING_FLAGS /W4 /permissive- /utf-8)
    if(BOUNCE_WERROR)
        list(APPEND BOUNCE_WARNING_FLAGS /WX)
    endif()
else()
    set(BOUNCE_WARNING_FLAGS -Wall -Wextra -Wpedantic -Wshadow -Wunused -Woverloaded-virtual
        -Wnon-virtual-dtor -Wimplicit-fallthrough -Wfloat-conversion)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        list(APPEND BOUNCE_WARNING_FLAGS -Wshorten-64-to-32)
    endif()
    if(BOUNCE_WERROR)
        list(APPEND BOUNCE_WARNING_FLAGS -Werror)
    endif()
endif()

# DSP and model sources: no dependency on the plug-in wrapper.
set(BOUNCE_CORE_SOURCES
    Source/Parameters.cpp
    Source/Pattern.cpp
    Source/OnsetDetector.cpp
    Source/Resampler.cpp
    Source/BounceEngine.cpp
    Source/Presets.cpp)

set(BOUNCE_PLUGIN_SOURCES
    ${BOUNCE_CORE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StateBridge.cpp)

set(BOUNCE_TEST_SOURCES
    Tests/TestMain.cpp
    Tests/PatternTests.cpp
    Tests/EngineTests.cpp
    Tests/StateTests.cpp)

set_source_files_properties(${BOUNCE_PLUGIN_SOURCES} ${BOUNCE_TEST_SOURCES} Tests/ShotHost.cpp
    PROPERTIES COMPILE_OPTIONS "${BOUNCE_WARNING_FLAGS}")

# The editor is a WebView; its page and JUCE's own frontend library are embedded, so nothing
# is fetched from a network at any time.
set(BOUNCE_COMMON_DEFS
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

# On Windows every target that compiles the editor needs WebView2, or it compiles against the
# legacy IE backend, which has no resource provider.
if(WIN32)
    list(APPEND BOUNCE_COMMON_DEFS JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1)
endif()

# index.js imports check_native_interop.js - BOTH must be embedded or the page's module graph
# fails silently and every control sits at its markup default.
set(BOUNCE_JUCE_JS_DIR ${BOUNCE_JUCE_DIR}/modules/juce_gui_extra/native/javascript)
file(GLOB BOUNCE_JUCE_JS ${BOUNCE_JUCE_JS_DIR}/*.js)
if(NOT BOUNCE_JUCE_JS)
    message(FATAL_ERROR "JUCE WebView frontend library not found in ${BOUNCE_JUCE_JS_DIR}")
endif()
juce_add_binary_data(BounceData SOURCES UI/index.html UI/styles.css UI/app.js ${BOUNCE_JUCE_JS})

# --- the plug-in ---------------------------------------------------------------------------
# PLUGIN_CODE and BUNDLE_ID are frozen once released: hosts identify the plug-in by them.
juce_add_plugin(Bounce
    COMPANY_NAME "Naaman"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    NEEDS_WEBVIEW2 TRUE
    COPY_PLUGIN_AFTER_BUILD ${BOUNCE_COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE Naam
    PLUGIN_CODE Bnce
    BUNDLE_ID com.naaman.bounce
    FORMATS ${BOUNCE_FORMATS}
    VST3_CATEGORIES "Fx" "Delay" "Pitch Shift"
    AU_MAIN_TYPE kAudioUnitType_Effect
    VERSION "${PROJECT_VERSION}"
    PRODUCT_NAME "BOUNCE")

target_sources(Bounce PRIVATE ${BOUNCE_PLUGIN_SOURCES})
target_compile_definitions(Bounce PUBLIC ${BOUNCE_COMMON_DEFS})
target_link_libraries(Bounce
    PRIVATE BounceData juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# --- test suite ----------------------------------------------------------------------------
#     build/BounceTests_artefacts/<config>/BounceTests
# Drives the pattern model, the engine and the whole AudioProcessor the way a host does.
juce_add_console_app(BounceTests PRODUCT_NAME "BounceTests" NEEDS_WEBVIEW2 TRUE)
juce_generate_juce_header(BounceTests)
target_sources(BounceTests PRIVATE ${BOUNCE_TEST_SOURCES} ${BOUNCE_PLUGIN_SOURCES})
target_compile_definitions(BounceTests PUBLIC ${BOUNCE_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(BounceTests
    PRIVATE BounceData juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags)

if(BOUNCE_SANITIZERS)
    if(MSVC)
        target_compile_options(BounceTests PRIVATE /fsanitize=address)
    else()
        target_compile_options(BounceTests PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(BounceTests PRIVATE -fsanitize=address,undefined)
    endif()
endif()

# --- screenshot host -----------------------------------------------------------------------
#     build/BounceShot_artefacts/<config>/BounceShot [seconds] [preset]
# The real editor on the real processor, fed drum hits in real time, for screenshots.
juce_add_console_app(BounceShot PRODUCT_NAME "BounceShot" NEEDS_WEBVIEW2 TRUE)
juce_generate_juce_header(BounceShot)
target_sources(BounceShot PRIVATE Tests/ShotHost.cpp ${BOUNCE_PLUGIN_SOURCES})
target_compile_definitions(BounceShot PUBLIC ${BOUNCE_COMMON_DEFS}
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(BounceShot
    PRIVATE BounceData juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC  juce::juce_recommended_config_flags)


