cmake_minimum_required(VERSION 3.13)

project(simpleRenderOverride)

find_package(Maya REQUIRED)

set(CMAKE_SHARED_LIBRARY_PREFIX "")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

add_library(simpleRenderOverride SHARED main.cpp)
target_compile_definitions(simpleRenderOverride PUBLIC LINUX)
target_link_libraries(
  simpleRenderOverride PRIVATE
      Maya::Foundation
      Maya::OpenMaya
      Maya::OpenMayaUI
      Maya::OpenMayaAnim
)

install(TARGETS simpleRenderOverride DESTINATION plugins)
