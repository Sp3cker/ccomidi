cmake_minimum_required(VERSION 3.24)
project(ccomidi C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PUGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugl")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
add_compile_options(-Wall -Wextra -Wunused-variable -Wunused-function -g)

# ---- clap-wrapper (for VST3 build) ----
# We only ship a CLAP SDK locally under third_party/clap, so clap-wrapper's
# `clap` imported target wouldn't otherwise exist in this build. Pull the
# wrapper down with FetchContent and let it wire the CLAP target alongside.
# Pinned to the same commit poryaaaa uses so both projects stay in lockstep.
include(FetchContent)
set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_STANDALONE FALSE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_BUILD_AUV2 FALSE CACHE BOOL "" FORCE)
FetchContent_Declare(
    clap_wrapper
    GIT_REPOSITORY https://github.com/free-audio/clap-wrapper.git
    GIT_TAG        62bf4f193d8dac354a946c1e0cffda9174317042
)
FetchContent_MakeAvailable(clap_wrapper)
if(TARGET clap-wrapper-compile-options)
    target_compile_options(clap-wrapper-compile-options INTERFACE
        -Wno-error=return-type
        # ccomidi uses C99-style designated initializers in C++ files;
        # AUv2 build adds -Werror which flags these as c++20-designator.
        -Wno-error=c++20-designator
    )
endif()

if(WIN32)
    set(PUGL_PLATFORM_SOURCES
        ${PUGL_DIR}/src/win.c
        ${PUGL_DIR}/src/win_gl.c
        ${PUGL_DIR}/src/win_stub.c
    )
elseif(APPLE)
    set(PUGL_PLATFORM_SOURCES
        ${PUGL_DIR}/src/mac.m
        ${PUGL_DIR}/src/mac_stub.m
        src/gui/metal/pugl_mac_metal.m
    )
else()
    set(PUGL_PLATFORM_SOURCES
        ${PUGL_DIR}/src/x11.c
        ${PUGL_DIR}/src/x11_gl.c
        ${PUGL_DIR}/src/x11_stub.c
    )
endif()

set(PUGL_SOURCES
    ${PUGL_DIR}/src/common.c
    ${PUGL_DIR}/src/internal.c
    ${PUGL_PLATFORM_SOURCES}
)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
)

if(APPLE)
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_metal.mm)
else()
    list(APPEND IMGUI_SOURCES ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
endif()

add_library(ccomidi_core
    src/core/sender_core.cpp
)

target_include_directories(ccomidi_core PUBLIC
    src
)

add_executable(ccomidi_core_tests
    src/tests/test_sender_core.cpp
)

target_link_libraries(ccomidi_core_tests PRIVATE
    ccomidi_core
)

add_library(ccomidi_gui STATIC
    ${PUGL_SOURCES}
    ${IMGUI_SOURCES}
    src/gui/imgui_impl_pugl.cpp
    src/gui/editor_shell.cpp
)

target_include_directories(ccomidi_gui PUBLIC
    src
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PUGL_DIR}/include
    ${PUGL_DIR}/src
)

target_link_libraries(ccomidi_gui PUBLIC clap)

target_compile_definitions(ccomidi_gui PUBLIC
    PUGL_STATIC
)

if(APPLE)
    set_source_files_properties(src/gui/editor_shell.cpp PROPERTIES LANGUAGE OBJCXX)
    target_link_libraries(ccomidi_gui PUBLIC
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
    )
elseif(WIN32)
    target_link_libraries(ccomidi_gui PUBLIC opengl32)
else()
    target_link_libraries(ccomidi_gui PUBLIC GL X11 Xcursor Xrandr Xext)
endif()

add_library(ccomidi MODULE
    src/plugin/ccomidi_plugin.cpp
    src/plugin/voicegroup_bridge.cpp
    src/gui/ccomidi_editor.cpp
)

target_link_libraries(ccomidi PRIVATE
    ccomidi_core
    ccomidi_gui
)

target_add_vst3_wrapper(
    TARGET ccomidi
    OUTPUT_NAME "ccomidi"
)

if(APPLE)
    set(CLAP_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set_target_properties(ccomidi PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
    )
    # codesign seals the bundle as ad-hoc so hardened-runtime plugin
    # scanners (Ableton) don't reject it with "code has no resources but
    # signature indicates they must be present".
    add_custom_command(TARGET ccomidi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Info.plist"
            "$<TARGET_BUNDLE_CONTENT_DIR:ccomidi>/Info.plist"
        COMMAND codesign -f -s - "$<TARGET_BUNDLE_DIR:ccomidi>"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${CLAP_INSTALL_DIR}/ccomidi.clap"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:ccomidi>"
            "${CLAP_INSTALL_DIR}/ccomidi.clap"
    )
endif()

set(MIDIFILE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/midifile")

add_library(midifile STATIC
    ${MIDIFILE_DIR}/src/MidiFile.cpp
    ${MIDIFILE_DIR}/src/MidiEvent.cpp
    ${MIDIFILE_DIR}/src/MidiEventList.cpp
    ${MIDIFILE_DIR}/src/MidiMessage.cpp
    ${MIDIFILE_DIR}/src/Binasc.cpp
    ${MIDIFILE_DIR}/src/Options.cpp
)

target_include_directories(midifile PUBLIC
    ${MIDIFILE_DIR}/include
)

add_library(ccomidi_recorder_core STATIC
    src/recorder/recorder_core.cpp
    src/recorder/smf_writer.cpp
)

target_include_directories(ccomidi_recorder_core PUBLIC
    src
)

target_link_libraries(ccomidi_recorder_core PRIVATE
    midifile
)

add_executable(ccomidi_recorder_tests
    src/tests/test_recorder_smf.cpp
)

target_link_libraries(ccomidi_recorder_tests PRIVATE
    ccomidi_recorder_core
    midifile
)

add_library(ccomidi_recorder MODULE
    src/plugin/recorder/recorder_plugin.cpp
    src/plugin/recorder/recorder_editor.cpp
)

target_link_libraries(ccomidi_recorder PRIVATE
    ccomidi_recorder_core
    ccomidi_gui
    clap
)

target_add_vst3_wrapper(
    TARGET ccomidi_recorder
    OUTPUT_NAME "ccomidi_recorder"
)

if(APPLE)
    set_target_properties(ccomidi_recorder PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
    )
    add_custom_command(TARGET ccomidi_recorder POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/recorder/Info.plist"
            "$<TARGET_BUNDLE_CONTENT_DIR:ccomidi_recorder>/Info.plist"
        COMMAND codesign -f -s - "$<TARGET_BUNDLE_DIR:ccomidi_recorder>"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${CLAP_INSTALL_DIR}/ccomidi_recorder.clap"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:ccomidi_recorder>"
            "${CLAP_INSTALL_DIR}/ccomidi_recorder.clap"
    )
endif()

# ---- VST3 targets (clap-wrapper) ----
# Each VST3 target rebuilds the plugin sources alongside clap-wrapper's
# VST3 entry. The wrapper discovers our plugin via the `clap_entry` symbol
# defined in the respective `*_plugin.cpp`, so the same source is compiled
# once per format rather than extracted into a shared static library.
if(APPLE)
    set(VST3_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/VST3")
endif()

# -- ccomidi (note effect / MIDI CC sender) --
add_library(ccomidi-vst3 MODULE
    src/plugin/ccomidi_plugin.cpp
    src/plugin/voicegroup_bridge.cpp
    src/gui/ccomidi_editor.cpp
)

target_link_libraries(ccomidi-vst3 PRIVATE
    ccomidi_core
    ccomidi_gui
)

target_add_vst3_wrapper(
    TARGET ccomidi-vst3
    OUTPUT_NAME "ccomidi"
    BUNDLE_IDENTIFIER "com.sallegrezza.ccomidi.vst3"
    BUNDLE_VERSION "1.0"
)

if(APPLE)
    add_custom_command(TARGET ccomidi-vst3 POST_BUILD
        COMMAND codesign -f -s - "$<TARGET_BUNDLE_DIR:ccomidi-vst3>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${VST3_INSTALL_DIR}/ccomidi.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:ccomidi-vst3>"
            "${VST3_INSTALL_DIR}/ccomidi.vst3"
    )
endif()

# -- ccomidi_recorder (MIDI pass-through that captures to SMF) --
add_library(ccomidi_recorder-vst3 MODULE
    src/plugin/recorder/recorder_plugin.cpp
    src/plugin/recorder/recorder_editor.cpp
)

target_include_directories(ccomidi_recorder-vst3 PRIVATE
    ${CLAP_SDK_DIR}
)

target_link_libraries(ccomidi_recorder-vst3 PRIVATE
    ccomidi_recorder_core
    ccomidi_gui
)

target_add_vst3_wrapper(
    TARGET ccomidi_recorder-vst3
    OUTPUT_NAME "ccomidi_recorder"
    BUNDLE_IDENTIFIER "com.sallegrezza.ccomidi.recorder.vst3"
    BUNDLE_VERSION "1.0"
)

if(APPLE)
    add_custom_command(TARGET ccomidi_recorder-vst3 POST_BUILD
        COMMAND codesign -f -s - "$<TARGET_BUNDLE_DIR:ccomidi_recorder-vst3>"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${VST3_INSTALL_DIR}"
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${VST3_INSTALL_DIR}/ccomidi_recorder.vst3"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:ccomidi_recorder-vst3>"
            "${VST3_INSTALL_DIR}/ccomidi_recorder.vst3"
    )
endif()

