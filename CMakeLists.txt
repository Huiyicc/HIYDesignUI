cmake_minimum_required(VERSION 3.20)
project(HIYDesignUI LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
if(NOT MSVC)
    enable_language(ASM)
endif()
if(NOT TARGET hiyui::hiyui)
    set(HIYUI_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." CACHE PATH "HIY-UI source directory")
    add_subdirectory("${HIYUI_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/hiyui")
endif()
if(NOT HIYUI_ENABLE_XML_LAYOUT OR NOT HIYUI_BACKEND STREQUAL "SDL" OR ANDROID OR EMSCRIPTEN)
    message(FATAL_ERROR "HIYDesignUI requires desktop SDL and HIYUI_ENABLE_XML_LAYOUT")
endif()

add_executable(HIYDesignUI
    src/main.cpp
    src/designer_app.cpp
    src/component_tree.cpp
    src/node_editing.cpp
    src/design_document.cpp
    src/component_catalog.cpp
    src/design_surface.cpp
    src/property_inspector.cpp
    src/project_io.cpp
    src/editor_palette.cpp
)
target_link_libraries(HIYDesignUI PRIVATE cppmodule::pugixml)
hiyui_setup_application(HIYDesignUI)
if(MSVC)
    target_compile_options(HIYDesignUI PRIVATE /utf-8)
endif()

