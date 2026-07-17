cmake_minimum_required(VERSION 3.20)
project(BoltEngine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BOLT_USE_VULKAN "Build with Vulkan rendering" ON)
option(BOLT_BUILD_TOOLS "Build offline asset tools (Grok pipeline)" ON)

# -----------------------------------------------------------------------------
# Dependencies — prefer find_package (vcpkg toolchain recommended)
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/cmake
# -----------------------------------------------------------------------------
find_package(glm CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# entt is header-only; allow vendored path
set(ENTT_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/third_party/entt/single_include" CACHE PATH "entt include")
if(NOT EXISTS "${ENTT_INCLUDE_DIR}/entt/entt.hpp")
  message(STATUS "entt not in third_party — set ENTT_INCLUDE_DIR or add submodule")
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
  src/pcg/PathGenerator.cpp
  src/pcg/VegetationSpawner.cpp
  src/ecs/Systems.cpp
  src/assets/MaterialLibrary.cpp
  src/assets/TextureLoader.cpp
  src/render/QualityTier.cpp
  src/render/RenderGraph.cpp
  src/render/VulkanContext.cpp
  src/app/Application.cpp
)

target_include_directories(bolt_engine
  PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${ENTT_INCLUDE_DIR}
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

# -----------------------------------------------------------------------------
# Game executable — Crystal Nebula vertical slice
# -----------------------------------------------------------------------------
add_executable(bolt_crystal
  apps/crystal_plains/main.cpp
)

target_link_libraries(bolt_crystal PRIVATE bolt_engine)

# -----------------------------------------------------------------------------
# Offline tools
# -----------------------------------------------------------------------------
if(BOLT_BUILD_TOOLS)
  add_executable(bolt_grok_import
    tools/grok_import/main.cpp
    tools/grok_import/GrokMaterialPipeline.cpp
  )
  target_link_libraries(bolt_grok_import PRIVATE bolt_engine)
  target_include_directories(bolt_grok_import PRIVATE ${CMAKE_SOURCE_DIR}/include)
endif()

# -----------------------------------------------------------------------------
# Assets copy for runtime
# -----------------------------------------------------------------------------
add_custom_command(TARGET bolt_crystal POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets
    $<TARGET_FILE_DIR:bolt_crystal>/assets
  COMMENT "Copy assets next to bolt_crystal"
)

install(TARGETS bolt_crystal RUNTIME DESTINATION bin)
install(DIRECTORY assets/ DESTINATION share/bolt_engine/assets)
