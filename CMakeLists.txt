cmake_minimum_required(VERSION 3.20)
project(BoltEngine VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BOLT_USE_VULKAN "Build with Vulkan rendering" ON)
option(BOLT_BUILD_TOOLS "Build offline asset tools" ON)

include(FetchContent)

# -----------------------------------------------------------------------------
# Dependencies via FetchContent (works without MSVC/vcpkg)
# -----------------------------------------------------------------------------
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG 1.0.1
  GIT_SHALLOW TRUE
)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG 3.4
  GIT_SHALLOW TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(json)

# entt header-only
set(ENTT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/entt/single_include")
if(NOT EXISTS "${ENTT_INCLUDE_DIR}/entt/entt.hpp")
  message(FATAL_ERROR "entt missing — run scripts/fetch_entt.ps1")
endif()

if(BOLT_USE_VULKAN)
  find_package(Vulkan REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Engine library
# -----------------------------------------------------------------------------
add_library(bolt_engine
  src/core/Log.cpp
  src/core/Time.cpp
  src/sprint/SprintCore.cpp
  src/sprint/SpawnBudgets.cpp
  src/world/HeightField.cpp
  src/world/SpawnRules.cpp
  src/world/TerrainMesh.cpp
  src/pcg/PathGenerator.cpp
  src/pcg/VegetationSpawner.cpp
  src/pcg/StalkMesh.cpp
  src/pcg/MeshPrimitives.cpp
  src/pcg/BoltGsd.cpp
  src/ecs/Systems.cpp
  src/assets/MaterialLibrary.cpp
  src/assets/TextureLoader.cpp
  src/assets/ObjLoader.cpp
  src/render/QualityTier.cpp
  src/render/RenderGraph.cpp
  src/render/VulkanContext.cpp
  src/app/Application.cpp
)

target_include_directories(bolt_engine
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ENTT_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/stb
)

target_link_libraries(bolt_engine
  PUBLIC
    glm::glm
    glfw
    nlohmann_json::nlohmann_json
)

if(BOLT_USE_VULKAN)
  target_compile_definitions(bolt_engine PUBLIC BOLT_VULKAN=1)
  target_link_libraries(bolt_engine PUBLIC Vulkan::Vulkan)
endif()

if(WIN32)
  target_compile_definitions(bolt_engine PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# -----------------------------------------------------------------------------
# Game executable
# -----------------------------------------------------------------------------
add_executable(bolt_crystal
  apps/crystal_plains/main.cpp
)
target_link_libraries(bolt_crystal PRIVATE bolt_engine)

# Copy assets next to binary
add_custom_command(TARGET bolt_crystal POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:bolt_crystal>/assets
  COMMENT "Copy assets next to bolt_crystal"
)

# -----------------------------------------------------------------------------
# Tools
# -----------------------------------------------------------------------------
if(BOLT_BUILD_TOOLS)
  # Standalone: real Imagine → PBR (no Vulkan link required)
  add_executable(bolt_grok_import
    tools/grok_import/main.cpp
    tools/grok_import/GrokMaterialPipeline.cpp
    tools/grok_import/ImageOps.cpp
  )
  target_include_directories(bolt_grok_import PRIVATE
    ${CMAKE_SOURCE_DIR}/tools/grok_import
    ${CMAKE_SOURCE_DIR}/third_party/stb
  )

  add_executable(bolt_make_sample_ground
    tools/make_sample_ground/main.cpp
  )
endif()

install(TARGETS bolt_crystal RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/bolt_engine/assets)
