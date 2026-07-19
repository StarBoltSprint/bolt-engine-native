# Bolt Engine (Native)

High-performance **C++ / Vulkan / entt** engine for **StarBoltSprint**.

> *Sprint shapes the world.* Meaningful Sprint Score drives procedural density, beauty, and chaos — starting with **Crystal Nebula Plains**.

## Play the game (Windows)

**Download a ready-to-run build** from [Releases](https://github.com/StarBoltSprint/bolt-engine-native/releases):

1. Download **`BoltCrystalPlains-windows-x64.zip`**
2. Unzip
3. Run **`bolt_crystal.exe`** (keep the `assets/` folder next to it)

Optional: `bolt_crystal.exe --fullscreen` or press **F11** / **Alt+Enter**.

### Requirements
- Windows 10/11 **64-bit**
- **Vulkan** GPU drivers (NVIDIA / AMD / Intel — keep them updated)
- [VC++ Redistributable x64](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist) if the OS asks for `MSVCP140.dll`

### Controls
| Key | Action |
|-----|--------|
| WASD | Move |
| Shift | Sprint |
| Space | Jump |
| RMB drag | Orbit camera |
| Scroll | Zoom |
| F11 / Alt+Enter | Fullscreen |
| Esc | Leave fullscreen, then quit |

### Offline — no xAI / Grok API

This game **does not call any cloud API** (no xAI, no Grok, no OpenAI).  
It never needs an API key and **never spends credits**.

Names like “Grok pipeline” in docs/tools only mean: offline conversion of PNG textures you already have on disk. The shipped `.exe` loads local assets only.

## Two separate projects

| Project | Stack | Play now? | Links |
|---------|--------|-----------|--------|
| **Three.js prototype** | Browser | **Yes — play in browser** | [Repo](https://github.com/StarBoltSprint/boltverse-threejs-game) · [Play](https://starboltsprint.github.io/boltverse-threejs-game/) |
| **This repo (native)** | C++ / Vulkan | **Yes — Windows release zip** or build from source | [Releases](https://github.com/StarBoltSprint/bolt-engine-native/releases) |

## Status

**Crystal Nebula Plains** vertical slice: path corridor, streamed terrain, vegetation + details + flying props, ruins, deferred lighting / shadows / SSAO, Crystal Nebula sky, animated Bolt GSD mesh, fullscreen.

## Build from source

```powershell
# Needs: CMake, VS 2022 Build Tools (C++), Vulkan SDK
# 1) entt headers
.\scripts\fetch_entt.ps1

# 2) Configure & build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target bolt_crystal -j 8

# 3) Run (cwd should see assets/ — build/ copies them next to the exe)
.\build\bolt_crystal.exe
# or
.\build\bolt_crystal.exe --fullscreen
```

Compile shaders after GLSL edits:

```powershell
.\scripts\compile_shaders.ps1
```

## Docs

| Doc | Content |
|-----|---------|
| [docs/PLAN.md](docs/PLAN.md) | Architecture + migration plan |
| [docs/FOLDER_STRUCTURE.md](docs/FOLDER_STRUCTURE.md) | Repo tree |
| [docs/GROK_PIPELINE.md](docs/GROK_PIPELINE.md) | Offline Imagine PNG → PBR import (**local only**) |

## Scope (Crystal Nebula)

- Terrain height + main path corridor + veg exclusion  
- Chunk streaming, ruins, detail props, flying wisps  
- SprintCore + spawn budgets  
- Deferred PBR, shadows, SSAO / post, sky micro-detail  
- Bolt HQ fur materials + gait / turn animation  
- Offline material import tool (no network)

## License

Prototype code — StarBoltSprint / xAI collaboration spirit.
