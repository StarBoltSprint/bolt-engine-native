# Grok Imagine → PBR Material Pipeline

## Goal

Convert **Grok Imagine** concept images into **natural integrated** PBR materials for procedural geometry (terrain, bark, crystal stalks) — not “quad with a sticker.”

## Offline flow

```
1. Prompt Grok Imagine for seamless / tileable surfaces (same biome palette)
   ground / path / rock / stalk under crystal_nebula

2. Save PNG → assets/grok_inbox/crystal_*_src.png

3. Run pack import:
   scripts/run_grok_pipeline.ps1
   (or bolt_grok_import --in ... --out assets/materials/crystal_nebula --name crystal_ground)

4. Tool generates per name:
   name_albedo / height / normal / roughness / metallic.png + name.json

5. Runtime: VulkanContext::loadBiomeMaterials loads ground+rock+path+stalk;
   terrain.frag blends path ribbon + slope rock over ground triplanar;
   foliage.frag samples stalk maps on instances.
```

## Crystal Nebula pack

| Name | Role |
|------|------|
| `crystal_ground` | Base terrain triplanar |
| `crystal_rock` | Slope / cliff mix (by normal.y) |
| `crystal_path` | Soft S-curve sprint ribbon (world-space mask) |
| `crystal_stalk` | Foliage instance material |

## Making it look natural on procedural meshes

| Technique | Why |
|-----------|-----|
| **Triplanar mapping** | No obvious UV seams on noise terrain |
| **Slope rock mix** | Hard mineral on steep faces |
| **Path ribbon mask** | Readable sprint corridor without painted geometry |
| **Detail normal** | Second tiling scale breaks repetition at sprint speed |
| **Roughness variation** | Avoid plastic flat look |
| **Emissive tip (stalks)** | Crystal glow only on tips / kinds |

## Quality tips for Imagine prompts

- Always ask for **seamless / tileable**, orthographic or top-down for ground  
- Keep **one palette** — edit path/rock/stalk from the ground reference  
- Path: uniform surface (no baked center stripe — engine applies the ribbon mask)  
- Separate **bark** / **stalk** as their own material sets  

## Integration with ECS

`MaterialId` on `MeshRenderer` / `TerrainChunk` / `FoliageBatch` components.  
`MaterialLibrary::get(MaterialId)` returns GPU descriptors for the RenderGraph pass.
