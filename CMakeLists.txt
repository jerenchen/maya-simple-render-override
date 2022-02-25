cmake_minimum_required(VERSION 2.8)

project(simpleRenderOverride)

include($ENV{DEVKIT_LOCATION}/cmake/pluginEntry.cmake)

set(SOURCE_FILES main.cpp)

set(LIBRARIES
  OpenMaya
  Foundation
  OpenMayaUI
  OpenMayaAnim
  OpenMayaRender
)

build_plugin()
