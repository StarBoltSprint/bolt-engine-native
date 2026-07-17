# Folder structure

```
bolt-engine-native/
├── CMakeLists.txt
├── README.md
├── docs/
│   ├── PLAN.md                 # Architecture + migration
│   ├── FOLDER_STRUCTURE.md
│   └── GROK_PIPELINE.md
├── include/bolt/
│   ├── core/
│   │   ├── Log.hpp
│   │   └── Time.hpp
│   ├── sprint/
│   │   ├── SprintCore.hpp      # Meaningful score + prediction
│   │   └── SpawnBudgets.hpp
│   ├── world/
│   │   ├── HeightField.hpp
│   │   └── SpawnRules.hpp      # CLEAR bubble + corridor
│   ├── pcg/
│   │   ├── PathGenerator.hpp
│   │   └── VegetationSpawner.hpp
│   ├── ecs/
│   │   ├── Components.hpp
│   │   └── Systems.hpp
│   ├── assets/
│   │   ├── MaterialLibrary.hpp
│   │   └── TextureLoader.hpp
│   ├── render/
│   │   ├── QualityTier.hpp
│   │   ├── RenderGraph.hpp
│   │   └── VulkanContext.hpp
│   └── app/
│       └── Application.hpp
├── src/                        # Mirrors include/
├── apps/
│   └── crystal_plains/
│       └── main.cpp
├── tools/
│   └── grok_import/
│       ├── main.cpp
│       └── GrokMaterialPipeline.*
├── assets/
│   ├── materials/
│   │   └── crystal_nebula/
│   │       └── ground.json     # Manifest (maps filled after Grok import)
│   ├── shaders/
│   │   ├── terrain.vert.glsl
│   │   ├── terrain.frag.glsl
│   │   └── foliage.vert.glsl
│   └── grok_inbox/             # Drop raw Imagine PNGs here
├── third_party/
│   └── entt/                   # git submodule
└── scripts/
    └── fetch_entt.ps1
```
