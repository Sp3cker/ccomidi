cmake_minimum_required(VERSION 3.24)
project(ccomidi C CXX OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(PUGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pugl")
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/imgui")

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

set(CLAP_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clap/include")

add_library(ccomidi MODULE
    src/plugin/ccomidi_plugin.cpp
    src/gui/ccomidi_editor.cpp
    src/gui/imgui_impl_pugl.cpp
    ${IMGUI_SOURCES}
    ${PUGL_SOURCES}
)

target_include_directories(ccomidi PRIVATE
    src
    ${CLAP_SDK_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${PUGL_DIR}/include
    ${PUGL_DIR}/src
)

if(APPLE)
    set_source_files_properties(src/gui/ccomidi_editor.cpp PROPERTIES LANGUAGE OBJCXX)
endif()

target_link_libraries(ccomidi PRIVATE
    ccomidi_core
)

target_compile_definitions(ccomidi PRIVATE
    PUGL_STATIC
)

set_target_properties(ccomidi PROPERTIES
    PREFIX ""
    SUFFIX ".clap"
)

if(APPLE)
    set_target_properties(ccomidi PROPERTIES
        BUNDLE TRUE
        BUNDLE_EXTENSION "clap"
    )
    target_link_libraries(ccomidi PRIVATE
        "-framework Cocoa"
        "-framework Metal"
        "-framework QuartzCore"
    )
    add_custom_command(TARGET ccomidi POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/src/plugin/Info.plist"
            "$<TARGET_BUNDLE_CONTENT_DIR:ccomidi>/Info.plist"
    )
elseif(WIN32)
    target_link_libraries(ccomidi PRIVATE opengl32)
else()
    target_link_libraries(ccomidi PRIVATE GL X11 Xcursor Xrandr Xext)
endif()
