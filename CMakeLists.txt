cmake_minimum_required(VERSION 3.24)
project(ccomidi C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PUGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugl")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
add_compile_options(-Wall -Wextra -Wunused-variable -Wunused-function -g)

# ---- CLAP SDK ----
set(CLAP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/clap)

# ---- clap-wrapper (produces VST3 bundle from CLAP plugin) ----
# Apply vendored clap-wrapper patches idempotently. The patch adds a
# CLAP_EVENT_MIDI <-> VST3 event bridge (output-side and input-side) that
# upstream clap-wrapper does not yet ship; see patches/README for details.
# Marker `kLegacyMIDICCOutEvent` appears only after the patch is applied,
# so the check is safe against re-runs and against `git submodule update`.
set(_ccomidi_wrapper_proc
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap-wrapper/src/detail/vst3/process.cpp")
if(EXISTS "${_ccomidi_wrapper_proc}")
    file(READ "${_ccomidi_wrapper_proc}" _ccomidi_wrapper_content)
    if(NOT _ccomidi_wrapper_content MATCHES "kLegacyMIDICCOutEvent")
        execute_process(
            COMMAND git apply --verbose
                    "${CMAKE_CURRENT_SOURCE_DIR}/patches/clap-wrapper-midi-event-bridge.patch"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap-wrapper"
            RESULT_VARIABLE _ccomidi_patch_result
            ERROR_VARIABLE  _ccomidi_patch_err)
        if(NOT _ccomidi_patch_result EQUAL 0)
            message(FATAL_ERROR
                "Failed to apply clap-wrapper patch: ${_ccomidi_patch_err}")
        endif()
        message(STATUS "ccomidi: applied clap-wrapper MIDI-event-bridge patch")
    endif()
endif()

set(CLAP_WRAPPER_DOWNLOAD_DEPENDENCIES TRUE CACHE BOOL "" FORCE)
set(CLAP_WRAPPER_COPY_AFTER_BUILD TRUE CACHE BOOL "" FORCE)
add_subdirectory(third_party/clap-wrapper)

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
