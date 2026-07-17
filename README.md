# Bolt Engine (Native)

High-performance **C++ / Vulkan / entt** engine for **StarBoltSprint**.

> *Sprint shapes the world.* Meaningful Sprint Score drives procedural density, beauty, and chaos — starting with **Crystal Nebula Plains**.

## Two separate projects

| Project | Stack | Play now? | Links |
|---------|--------|-----------|--------|
| **Three.js prototype** | Browser | **Yes — play in browser** | [Repo](https://github.com/StarBoltSprint/boltverse-threejs-game) · [Play](https://starboltsprint.github.io/boltverse-threejs-game/) |
| **This repo (native)** | C++ / Vulkan | **Not a full game yet** — scaffold to build & run a window / systems | [This repo](https://github.com/StarBoltSprint/bolt-engine-native) |

The web game stays as-is. This native repo does **not** replace the playable link until we finish rendering.

## Status

**Scaffold / Phase 0** — architecture, ECS, SprintCore, MaterialLibrary, RenderGraph stubs, Grok import tool skeleton.  
Full Vulkan draw loop requires local Vulkan SDK + vcpkg packages.

## Quick start

```bash
# 1) Install deps (example: vcpkg)
vcpkg install glm glfw3 nlohmann-json

# 2) entt (header-only)
git submodule add https://github.com/skypjack/entt.git third_party/entt
# or unzip entt single_include into third_party/entt/single_include

# 3) Configure & build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=[vcpkg]/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Release

# 4) Run Crystal slice
./build/Release/bolt_crystal   # or build/bolt_crystal
```

## Docs

| Doc | Content |
|-----|---------|
| [docs/PLAN.md](docs/PLAN.md) | Full architecture + migration plan |
| [docs/FOLDER_STRUCTURE.md](docs/FOLDER_STRUCTURE.md) | Tree of the repo |
| [docs/GROK_PIPELINE.md](docs/GROK_PIPELINE.md) | Imagine → PBR materials |

## Scope now (Crystal Nebula)

- Terrain height + path corridor  
- Instanced vegetation (flanks only)  
- SprintCore + spawn budgets  
- MaterialLibrary + offline Grok import tool  
- Bolt lightning aura (placeholder mesh)  

## License

Prototype code — StarBoltSprint / xAI collaboration spirit.  
