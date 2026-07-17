# Bolt Engine Native — Starter Plan (C++ / Vulkan / entt)

**Goal:** Migrate the Three.js StarBoltSprint prototype into a high-performance native engine where **sprint shapes the world**.

**Phase 0 scope (this repo):** Crystal Nebula Plains only — terrain, vegetation, paths, basic Bolt lightning aura, Meaningful Sprint Score + procedural spawner core.

---

## 1. Design principles

| Principle | Meaning |
|-----------|---------|
| **Sprint is the sim clock** | `MeaningfulSprintScore` (0–1+) drives density, LOD bias, particle budget, warp strength, path refresh |
| **Predict ahead, never under feet** | Spawner uses velocity prediction + hard exclusion zone + clear path corridor |
| **GPU where it scales** | Height / instance culling / particles on GPU; CPU places sparse “heroes” (ruins later) |
| **Assets are materials, not stickers** | Grok Imagine → full PBR set + triplanar / blend so geometry doesn’t look UV-stamped |
| **One biome first** | Ship Crystal feel before Ember/Whisper complexity |

---

## 2. Architecture layers

```
App (GLFW window, input, loop)
  └─ Engine
       ├─ ECS (entt registry + systems ordered by phase)
       ├─ SprintCore (score, prediction, budgets)
       ├─ World / PCG (height, path, vegetation spawner)
       ├─ Render (Vulkan device, RenderGraph, materials, GPU terrain)
       └─ Assets (MaterialLibrary, hot-reload, Grok pipeline import)
```

### Frame phases (ordered)

1. **Input** — WASD, Shift sprint, look  
2. **SprintCore** — update score, prediction cone, density budget  
3. **Simulation** — player motion (fixed step), exclusion clear  
4. **PCG** — spawn/despawn vegetation & path segments in prediction cone  
5. **RenderGraph** — cull → depth prepass → terrain → foliage → paths → Bolt → post (motion blur optional)  
6. **AssetWatch** — hot-reload materials (editor / debug)

---

## 3. Migration from Three.js (step-by-step)

| Step | Three.js source | Native target | Done when |
|------|-----------------|---------------|-----------|
| M1 | `MeaningfulSprintScore` in `procedural.js` | `SprintCore` | Score 0–1 tracks speed/momentum |
| M2 | `surfaceHeight` / Crystal profile | `HeightField` + GPU height shader | Walkable height samples match mesh |
| M3 | PathGenerator energy path | `PathSystem` + ribbon mesh | Glowing path ahead of Bolt |
| M4 | Vegetation + forestDensity | `VegetationSystem` + GPU instances | Flank flora, clear corridor |
| M5 | CLEAR exclusion / corridor | `SpawnRules` | Never spawn on Bolt / highway |
| M6 | Graphics Low–MAX | `QualityTier` | Budgets scale with tier |
| M7 | Grok / hand textures | `MaterialLibrary` + Grok pipeline | Crystal ground/bark look integrated |
| M8 | Full biomes, citadel, orbital | Later milestones | After Crystal vertical slice |

**Do not port:** HTML HUD first (use ImGui debug), bloom stacks, all 7 biomes, orbital debris.

---

## 4. Performance rules for high-speed sprint

- Fixed **1/60 physics**; catch up max N substeps (same lesson as web freeze fix)  
- **At most 1–2 chunk builds** or stream patches per frame  
- Vegetation: **instanced** only; no one-mesh-per-plant  
- LOD by distance + **sprint speed** (higher speed → lower LOD sooner)  
- Particle density ∝ `min(1, score)` but hard-capped by QualityTier  
- Despawn behind player aggressively  

---

## 5. Crystal Nebula vertical slice checklist

- [ ] Window + Vulkan device + swapchain  
- [ ] ECS player + transform + velocity  
- [ ] SprintCore score + budgets  
- [ ] Flat/height terrain patch (CPU height OK first; GPU later)  
- [ ] Path ribbon using exclusion corridor  
- [ ] Instanced stalks/crystals with MaterialLibrary  
- [ ] Bolt mesh + simple emissive aura  
- [ ] Grok → PBR folder import + hot-reload  
- [ ] Debug overlay: score, active instances, FPS  

---

## 6. Dependencies (vcpkg / system)

- **entt** — ECS  
- **Vulkan SDK** + **vk-bootstrap** (or raw init)  
- **GLFW** — window/input  
- **GLM** — math  
- **stb_image** — texture load  
- **nlohmann_json** — material manifests  
- **Optional:** VMA, SPIRV-Cross, meshoptimizer  

---

## 7. Next implementation order (after this scaffold)

1. Link Vulkan/GLFW and clear-color frame  
2. Upload a plane mesh with Crystal height  
3. Wire SprintCore to spawn instance counts  
4. Run Grok pipeline offline on 1 ground texture  
5. Match web CLEAR distances (meters)  

---

*Prototype reference: `../bolt-engine` (Three.js GitHub Pages build).*
