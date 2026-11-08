cmake_minimum_required(VERSION 3.22)

project(Comp29 VERSION 0.1.0 LANGUAGES C CXX)

option(COMP29_ENABLE_SANITIZERS "Instrument Comp29 sources and tests with ASan/UBSan" OFF)
# JUCE's external manifest helper is not sanitizer-linked. Sanitized plugins are
# loaded by the sanitizer-linked test host instead; release builds keep manifests.
if(COMP29_ENABLE_SANITIZERS)
    set(COMP29_VST3_AUTO_MANIFEST FALSE)
else()
    set(COMP29_VST3_AUTO_MANIFEST TRUE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Supply a JUCE source checkout to build offline; otherwise fetch the pinned release.
set(COMP29_JUCE_SOURCE_DIR "" CACHE PATH "Path to a JUCE source checkout")
if(COMP29_JUCE_SOURCE_DIR)
    add_subdirectory("${COMP29_JUCE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        # JUCE 8.0.17-security. Update deliberately, rather than following a branch.
        GIT_TAG e548e97a54a083acbfc330d584419fd074409045
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(JUCE)
endif()

set(COMP29_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND COMP29_FORMATS AU)
endif()

juce_add_plugin(Comp29
    COMPANY_NAME "Comp29 Audio"
    BUNDLE_ID "com.comp29.audio.comp29"
    PLUGIN_MANUFACTURER_CODE Cp29
    PLUGIN_CODE C029
    PRODUCT_NAME "Comp29"
    DESCRIPTION "Stereo bus compressor"
    FORMATS ${COMP29_FORMATS}
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${COMP29_VST3_AUTO_MANIFEST}
    VST3_CATEGORIES Fx Dynamics
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# Implemented incrementally in Steps 2, 3 and 4 respectively.
target_sources(Comp29 PRIVATE
    Source/DSP/SSLCompressor.h
    Source/DSP/SSLCompressor.cpp
    Source/PluginProcessor.h
    Source/PluginProcessor.cpp
    Source/PluginEditor.h
    Source/PluginEditor.cpp
)

target_compile_definitions(Comp29 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Comp29
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_warning_flags
    PUBLIC
        juce::juce_recommended_config_flags
)

# Explicit module includes in source files; no generated JuceHeader.h required.

option(COMP29_BUILD_TESTS "Build DSP, processor, editor and realtime tests" OFF)
if(COMP29_BUILD_TESTS)
    enable_testing()
    add_executable(Comp29DSPTests Tests/SSLCompressorTests.cpp Source/DSP/SSLCompressor.cpp)
    add_test(NAME Comp29DSPTests COMMAND Comp29DSPTests)

    # Reuse the plugin's compiled JUCE modules instead of compiling them twice.
    add_executable(Comp29ProcessorTests Tests/PluginProcessorTests.cpp)
    target_include_directories(Comp29ProcessorTests PRIVATE
        $<TARGET_PROPERTY:Comp29,INCLUDE_DIRECTORIES>)
    target_link_libraries(Comp29ProcessorTests PRIVATE Comp29)
    add_test(NAME Comp29ProcessorTests COMMAND Comp29ProcessorTests)

    add_executable(Comp29EditorTests Tests/PluginEditorTests.cpp)
    target_include_directories(Comp29EditorTests PRIVATE
        $<TARGET_PROPERTY:Comp29,INCLUDE_DIRECTORIES>)
    target_link_libraries(Comp29EditorTests PRIVATE Comp29)
    add_test(NAME Comp29EditorTests COMMAND Comp29EditorTests "${CMAKE_BINARY_DIR}/ui-snapshots")

    add_executable(Comp29RealtimeTests Tests/RealtimeTests.cpp)
    target_include_directories(Comp29RealtimeTests PRIVATE
        $<TARGET_PROPERTY:Comp29,INCLUDE_DIRECTORIES>)
    target_link_libraries(Comp29RealtimeTests PRIVATE Comp29)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_definitions(Comp29RealtimeTests PRIVATE COMP29_WRAP_REALTIME_CALLS=1)
        target_link_options(Comp29RealtimeTests PRIVATE
            -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
            -Wl,--wrap=pthread_mutex_lock -Wl,--wrap=pthread_mutex_trylock)
    endif()
    add_test(NAME Comp29RealtimeTests COMMAND Comp29RealtimeTests)
endif()

option(COMP29_BUILD_HOST_TESTS "Load the built VST3 through a separate JUCE host" OFF)
if(COMP29_BUILD_HOST_TESTS)
    if(NOT COMP29_BUILD_TESTS)
        message(FATAL_ERROR "COMP29_BUILD_HOST_TESTS requires COMP29_BUILD_TESTS=ON")
    endif()
    # A separate JUCE build is intentional: the shipping plugin has hosting off.
    juce_add_console_app(Comp29HostTests PRODUCT_NAME "Comp29 VST3 Host Tests")
    target_sources(Comp29HostTests PRIVATE Tests/PluginHostTests.cpp)
    target_compile_definitions(Comp29HostTests PRIVATE
        JUCE_PLUGINHOST_VST3=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(Comp29HostTests PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)
    add_dependencies(Comp29HostTests Comp29_VST3)
    add_test(NAME Comp29HostTests COMMAND Comp29HostTests "$<TARGET_FILE:Comp29_VST3>")
endif()

if(COMP29_ENABLE_SANITIZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Comp29 ASan/UBSan configuration requires GCC or Clang")
    endif()
    # Third-party JUCE objects remain reusable; instrument all project sources.
    set_source_files_properties(
        Source/DSP/SSLCompressor.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp
        Tests/SSLCompressorTests.cpp Tests/PluginProcessorTests.cpp
        Tests/PluginEditorTests.cpp Tests/PluginHostTests.cpp Tests/RealtimeTests.cpp
        PROPERTIES COMPILE_OPTIONS "-fsanitize=address,undefined;-fno-omit-frame-pointer")
    target_link_options(Comp29 INTERFACE -fsanitize=address,undefined)
    foreach(test Comp29DSPTests Comp29ProcessorTests Comp29EditorTests Comp29RealtimeTests Comp29HostTests)
        if(TARGET ${test})
            target_link_options(${test} PRIVATE -fsanitize=address,undefined)
        endif()
    endforeach()
endif()

include(packaging/Package.cmake)
