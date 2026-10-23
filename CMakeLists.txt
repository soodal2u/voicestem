cmake_minimum_required(VERSION 3.22)
project(VoiceStem VERSION 0.2.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
# Static CRT (here and in the Rust lib via +crt-static) so the plugin needs no VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(VOICESTEM_JUCE_DIR "C:/dev/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${VOICESTEM_JUCE_DIR} JUCE)

# --- DeepFilterNet3 models (MIT/Apache-2.0, embedded into the Rust wrapper) -----------------
set(DF_REV d375b2d8309e0935d165700c91da9de862a99c31)
set(MODEL_DIR ${CMAKE_BINARY_DIR}/models)
foreach(model IN ITEMS "DeepFilterNet3_onnx.tar.gz;c94d91f70911001c946e0fabb4aa9adc37045f45a03b56008cb0c8244cb63616"
                       "DeepFilterNet3_ll_onnx.tar.gz;5998e58e8ba0e09bb76986ef97b84afa065a571ef282d4a1222f341e3251cf3a")
    list(GET model 0 name)
    list(GET model 1 sha)
    if(NOT EXISTS ${MODEL_DIR}/${name})
        message(STATUS "Downloading ${name}")
        file(DOWNLOAD https://github.com/Rikorose/DeepFilterNet/raw/${DF_REV}/models/${name} ${MODEL_DIR}/${name}
             EXPECTED_HASH SHA256=${sha} SHOW_PROGRESS)
    endif()
endforeach()

# --- Rust staticlib (rust/) ------------------------------------------------------------------
find_program(CARGO cargo HINTS "$ENV{USERPROFILE}/.cargo/bin" REQUIRED)
set(RUST_TARGET_DIR ${CMAKE_BINARY_DIR}/rust)
set(DFWRAP_LIB ${RUST_TARGET_DIR}/release/dfwrap.lib)
set(RUST_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.toml)

execute_process(COMMAND ${CARGO} fetch --manifest-path ${RUST_MANIFEST} COMMAND_ERROR_IS_FATAL ANY)
add_custom_command(
    OUTPUT ${DFWRAP_LIB}
    COMMAND ${CMAKE_COMMAND} -E env VOICESTEM_MODEL_DIR=${MODEL_DIR} CARGO_TARGET_DIR=${RUST_TARGET_DIR}
            "RUSTFLAGS=-C target-feature=+crt-static"
            ${CARGO} build --release --manifest-path ${RUST_MANIFEST}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/rust/src/lib.rs ${RUST_MANIFEST} ${CMAKE_CURRENT_SOURCE_DIR}/rust/Cargo.lock
    COMMENT "Building DeepFilterNet wrapper (cargo)"
    USES_TERMINAL)
add_custom_target(dfwrap_build DEPENDS ${DFWRAP_LIB})

# windows-targets ships its import library inside the crate rather than the SDK.
file(GLOB WINDOWS_TARGETS_LIB "$ENV{USERPROFILE}/.cargo/registry/src/*/windows_x86_64_msvc-0.52.*/lib/windows.0.52.0.lib")
list(GET WINDOWS_TARGETS_LIB 0 WINDOWS_TARGETS_LIB)

add_library(dfwrap INTERFACE)
add_dependencies(dfwrap dfwrap_build)
target_link_libraries(dfwrap INTERFACE ${DFWRAP_LIB} ${WINDOWS_TARGETS_LIB}
                      bcrypt advapi32 kernel32 ntdll userenv ws2_32 dbghelp)

set(ENGINE_SOURCES Source/VoiceStemEngine.cpp Source/ReverbSplitter.cpp)

# --- Plugin ------------------------------------------------------------------------------------
juce_add_plugin(VoiceStem
    COMPANY_NAME "soodal"
    PLUGIN_MANUFACTURER_CODE Sood
    PLUGIN_CODE Vstm
    FORMATS VST3 Standalone
    PRODUCT_NAME "VoiceStem"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    VST3_CATEGORIES Fx Restoration
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(VoiceStem PRIVATE Source/PluginProcessor.cpp Source/PluginEditor.cpp ${ENGINE_SOURCES})
target_compile_definitions(VoiceStem PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(VoiceStem
    PRIVATE juce::juce_audio_utils juce::juce_dsp dfwrap
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# --- Offline test driver -------------------------------------------------------------------------
juce_add_console_app(VoiceStemCli PRODUCT_NAME "VoiceStemCli")
target_sources(VoiceStemCli PRIVATE tools/StemCli.cpp ${ENGINE_SOURCES})
target_compile_definitions(VoiceStemCli PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(VoiceStemCli
    PRIVATE juce::juce_audio_formats juce::juce_dsp dfwrap
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# --- Host-side smoke test: loads the built .vst3 like a DAW ------------------------------------
juce_add_console_app(VoiceStemHostCheck PRODUCT_NAME "VoiceStemHostCheck")
target_sources(VoiceStemHostCheck PRIVATE tools/HostCheck.cpp)
target_compile_definitions(VoiceStemHostCheck PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(VoiceStemHostCheck
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

juce_add_console_app(VoiceStemSnapshot PRODUCT_NAME "VoiceStemSnapshot")
target_sources(VoiceStemSnapshot PRIVATE tools/Snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp ${ENGINE_SOURCES})
target_compile_definitions(VoiceStemSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="VoiceStem")
target_link_libraries(VoiceStemSnapshot
    PRIVATE juce::juce_audio_processors juce::juce_audio_formats juce::juce_dsp juce::juce_gui_basics dfwrap
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
