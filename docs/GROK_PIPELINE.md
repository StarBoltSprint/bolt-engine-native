# Grok Imagine → PBR Material Pipeline

## Goal

Convert **Grok Imagine** concept images into **natural integrated** PBR materials for procedural geometry (terrain, bark, crystal stalks) — not “quad with a sticker.”

## Offline flow

```
1. Prompt Grok Imagine for seamless / tileable surface
   e.g. "seamless top-down crystal nebula teal moss and quartz ground, no characters, tileable"

2. Save PNG → assets/grok_inbox/crystal_ground_albedo_src.png

3. Run tool:
   bolt_grok_import --in assets/grok_inbox/crystal_ground_albedo_src.png \
                    --out assets/materials/crystal_nebula/ground \
                    --name crystal_ground

4. Tool writes:
   ground_albedo.png
   ground_normal.png      (from height/sobel approximation or external)
   ground_roughness.png
   ground_metallic.png
   ground_height.png
   ground.json            (MaterialLibrary manifest)

5. Runtime MaterialLibrary hot-reloads when ground.json or maps change
```

## Making it look natural on procedural meshes

| Technique | Why |
|-----------|-----|
| **Triplanar mapping** | No obvious UV seams on noise terrain |
| **Height blend** | Mix grass/crystal by height & slope |
| **Detail normal** | Second tiling scale breaks repetition at sprint speed |
| **Roughness variation** | Avoid plastic flat look |
| **Emissive mask (optional)** | Crystal veins only — never full-surface neon |

Shader responsibility: `terrain.frag.glsl` samples MaterialLibrary sets with triplanar + sprint-driven detail scale.

## Quality tips for Imagine prompts

- Always ask for **seamless / tileable**, orthographic or top-down for ground  
- Separate **albedo** (color only, soft lighting) from **height** (grayscale) when possible  
- Generate **bark** and **crystal stalk** as separate material sets  

## Integration with ECS

`MaterialId` on `MeshRenderer` / `TerrainChunk` / `FoliageBatch` components.  
`MaterialLibrary::get(MaterialId)` returns GPU descriptors for the RenderGraph pass.
