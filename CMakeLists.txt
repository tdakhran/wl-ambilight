cmake_minimum_required(VERSION 3.10)

project(wl-ambilight LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

string(APPEND CMAKE_CXX_FLAGS "-Wall -Werror")

# generate sources from export dmabuf proto
find_package(waylandpp REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_search_module(waylandpp REQUIRED wayland-scanner++)

set(PROTO_XML_EXPORT_DMABUF
    ${CMAKE_SOURCE_DIR}/modules/wlroots/protocol/wlr-export-dmabuf-unstable-v1.xml
)
set(PROTO_FILES_EXPORT_DMABUF_CLIENT "wayland-client-export-dmabuf.hpp"
                                     "wayland-client-export-dmabuf.cpp")
add_custom_command(
  OUTPUT ${PROTO_FILES_EXPORT_DMABUF_CLIENT} ${PROTO_FILES_EXPORT_DMABUF_SERVER}
  COMMAND wayland-scanner++ ${PROTO_XML_EXPORT_DMABUF}
          ${PROTO_FILES_EXPORT_DMABUF_CLIENT} -x wayland-client-protocol.hpp
  DEPENDS ${PROTO_XML_EXPORT_DMABUF})

add_library(wlr-export-dmabuf ${PROTO_FILES_EXPORT_DMABUF_CLIENT})
target_include_directories(wlr-export-dmabuf PUBLIC ${CMAKE_BINARY_DIR})
target_link_libraries(wlr-export-dmabuf INTERFACE Waylandpp::wayland-client++
                                                  Waylandpp::wayland-server++)

# service
find_library(GBM_LIBRARY_PATH gbm REQUIRED)

add_executable(wl-ambilight wayland/capture.cpp src/service.cpp)
target_include_directories(wl-ambilight PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(wl-ambilight wlr-export-dmabuf ${GBM_LIBRARY_PATH})
