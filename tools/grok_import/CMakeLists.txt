# Standalone offline tool — no Vulkan/GLFW required
cmake_minimum_required(VERSION 3.16)
project(bolt_grok_import_standalone LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

add_executable(bolt_grok_import
  main.cpp
  GrokMaterialPipeline.cpp
  ImageOps.cpp
)
target_include_directories(bolt_grok_import PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/third_party/stb
  ${CMAKE_CURRENT_LIST_DIR}/../../third_party/stb
)

# When included from root, CMAKE_SOURCE_DIR is engine root
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/stb/stb_image.h")
  target_include_directories(bolt_grok_import PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
endif()
