cmake_minimum_required(VERSION 3.24)
project(ccomidi C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PUGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugl")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")
set(CLAP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap/include")
add_compile_options(-Wall -Wextra -Wunused-variable -Wunused-function -g)

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
    ${CLAP_SDK_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PUGL_DIR}/include
    ${PUGL_DIR}/src
)

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

set_target_properties(ccomidi PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

if(APPLE)
    set(CLAP_INSTALL_DIR "$ENV{HOME}/Library/Audio/Plug-Ins/CLAP")
    set_target_properties(ccomidi PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
    )
    add_custom_command(TARGET ccomidi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Info.plist"
            "$<TARGET_BUNDLE_CONTENT_DIR:ccomidi>/Info.plist"
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

target_include_directories(ccomidi_recorder PRIVATE
    ${CLAP_SDK_DIR}
)

target_link_libraries(ccomidi_recorder PRIVATE
    ccomidi_recorder_core
    ccomidi_gui
)

set_target_properties(ccomidi_recorder PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
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
        COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${CLAP_INSTALL_DIR}/ccomidi_recorder.clap"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "$<TARGET_BUNDLE_DIR:ccomidi_recorder>"
            "${CLAP_INSTALL_DIR}/ccomidi_recorder.clap"
    )
endif()
