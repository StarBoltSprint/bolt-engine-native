# Bolt HQ Asset Pack — Manifest

Engine-ready pack for **Bolt** (white German Shepherd).  
Root: `assets/characters/bolt/pack_hq/`

## Folder layout

```
pack_hq/
├── IDENTITY.md                 # locked character bible
├── MANIFEST.md                 # this file
├── source/
│   └── bolt_base_front.jpg     # master identity reference
├── consistency/
│   ├── turnaround/
│   │   ├── bolt_turn_front.jpg
│   │   ├── bolt_turn_side_right.jpg
│   │   ├── bolt_turn_back.jpg
│   │   └── bolt_turn_3q.jpg
│   ├── details/
│   │   ├── bolt_detail_head.jpg
│   │   ├── bolt_detail_paws.jpg    # + Star Moss contact glow
│   │   └── bolt_detail_tail.jpg
│   └── poses/
│       ├── bolt_pose_sprint_side.jpg
│       ├── bolt_pose_sprint_starmoss.jpg
│       └── bolt_pose_jump.jpg
├── materials/
│   ├── bolt_fur.json
│   ├── bolt_fur_albedo.png         # 1024 tile
│   ├── bolt_fur_normal.png
│   ├── bolt_fur_roughness.png
│   ├── bolt_fur_metallic.png
│   ├── bolt_fur_emissive.png       # lightning veins + eye energy
│   ├── bolt_fur_height.png
│   └── *_src.jpg                   # Imagine sources
├── animation/
│   └── sprint/
│       ├── bolt_sprint_cycle_src.mp4
│       ├── frame_001.png … frame_073.png   # 12 fps harvest
│       └── cycle/
│           └── sprint_01.png … sprint_10.png  # loopable keyframes
└── fx/
    ├── aura/
    │   └── bolt_aura_ring.jpg
    └── trails/
        ├── bolt_fx_pawprint.jpg
        └── bolt_fx_crystal_dust.jpg
```

## 1. Character consistency

| File | Purpose |
|------|---------|
| turnaround/* | Front, right profile, back, 3/4 — same identity chain |
| details/* | Head, paws (moss contact), tail |
| poses/* | Sprint side, sprint on Star Moss, jump |

All edit-chained from `source/bolt_base_front.jpg`.

## 2. PBR materials (Vulkan)

Synced to **`assets/materials/bolt/`** for `loadBoltFurPBR`.

| Map | Use |
|-----|-----|
| albedo | White fur + cream/cool variation |
| normal | Fur direction / volume |
| roughness | Soft matte coat |
| metallic | Near-black (dielectric fur) |
| emissive | Purple/cyan lightning veins |
| height | Fur micro height for parallax/SSS assist |

Resolution: **1024²** PNGs (real-time friendly).

## 3. Animation

| Item | Spec |
|------|------|
| Source video | `animation/sprint/bolt_sprint_cycle_src.mp4` (6s, 720p) |
| Dense harvest | 73 frames @ 12 fps (`frame_###.png`) |
| Loop set | 10 keyframes in `cycle/sprint_##.png` |
| Intended cycle fps | ~10–12 for billboard / flipbook |

Motion emphasis: leg alternation, ear/tail stream, aura pulse.  
Ground interaction reference: `poses/bolt_pose_sprint_starmoss.jpg`.

## 4. Lightning aura & trails

| File | Use |
|------|-----|
| `fx/aura/bolt_aura_ring.jpg` | Additive/emissive aura sprite |
| `fx/trails/bolt_fx_pawprint.jpg` | Star Moss footprint stamp |
| `fx/trails/bolt_fx_crystal_dust.jpg` | Kick-up particle source |

## 5. Import into C++ Vulkan

1. **Fur PBR:** already loaded via `VulkanContext::loadBoltFurPBR("assets/materials/bolt/bolt_fur")`.
2. **3D mesh:** existing `bolt_gsd.glb` / GSD parts; keep identity maps on fur shader.
3. **Billboard / flipbook (optional):** sample `animation/sprint/cycle/sprint_##.png`.
4. **Particles:** pawprint + crystal dust textures for kind 1/0 particle atlases.
5. **Aura:** sample `bolt_aura_ring` as additive billboard parented to spine or use emissive map.

## Quality notes

- Style: high-end stylized 3D game / semi-realistic fur  
- Key color: `#1a1528`  
- Defects to watch: video frames may slight palette drift vs base — prefer cycle keyframes for UI; use base for identity  
- Height map is procedural-style fur grain (not a full unwrap UV layout)

## Regenerating

```powershell
# After new Imagine exports into sessions/images, re-run packaging or
# scripts/compile_shaders.ps1 does not affect this pack.
```
