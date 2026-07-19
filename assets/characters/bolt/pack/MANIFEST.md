# Bolt Asset Pack v1 — Engine Ready

**Character:** Bolt — majestic white German Shepherd  
**Engine:** Bolt Engine Native (C++ / Vulkan)  
**Style:** High-end stylized 3D game character, volumetric fur, purple+cyan cosmic aura  
**Root:** `assets/characters/bolt/pack/`

See also: [IDENTITY.md](./IDENTITY.md)

---

## Folder map

```
pack/
├── IDENTITY.md                 # locked design bible + asymmetry map
├── MANIFEST.md                 # this file
├── source/
│   └── bolt_base.jpg           # master identity (edit-chain root)
├── consistency/
│   ├── turnaround/
│   │   ├── bolt_turn_front.jpg
│   │   ├── bolt_turn_side_right.jpg
│   │   ├── bolt_turn_back.jpg
│   │   ├── bolt_turn_3q.jpg
│   │   └── bolt_base_front.jpg
│   ├── poses/
│   │   ├── bolt_pose_sprint_side.jpg      # animation base
│   │   ├── bolt_pose_sprint_contact.jpg
│   │   └── bolt_pose_sprint_starmoss.jpg  # paw × Star Moss key art
│   └── details/
│       ├── bolt_detail_head.jpg
│       ├── bolt_detail_paws.jpg
│       └── bolt_detail_tail.jpg
├── materials/                  # 1024² Vulkan PBR (also synced → assets/materials/bolt/)
│   ├── bolt_fur.json
│   ├── bolt_fur_albedo.png
│   ├── bolt_fur_normal.png
│   ├── bolt_fur_roughness.png
│   ├── bolt_fur_height.png
│   ├── bolt_fur_metallic.png
│   ├── bolt_fur_emissive.png
│   └── *_src.jpg               # Imagine source masters
├── animation/
│   ├── sprint/
│   │   ├── bolt_sprint_cycle_src.mp4   # 6s @ 720p source
│   │   ├── frame_001.png …             # dense harvest @ 12 fps (~73)
│   │   └── cycle/sprint_01.png … 10    # selected loopable keyframes
│   ├── idle/
│   │   └── bolt_idle_stand.jpg
│   └── jump/
│       └── bolt_jump_apex.jpg
└── fx/
    ├── aura/
    │   └── bolt_aura_ring.jpg          # additive-friendly aura element
    └── trails/
        ├── bolt_fx_pawprint.jpg        # Star Moss footprint
        └── bolt_fx_crystal_dust.jpg    # kicked crystal dust
```

---

## How this pack was made (skills)

| Deliverable | Skill / method |
|-------------|----------------|
| Turnaround + details + poses | **game-character-consistency** — one base → `image_edit` chain |
| Engine defaults (flat bg, isolation, naming) | **game-asset-core** |
| Sprint cycle | **game-animation-frames** — video-first: side sprint pose → `image_to_video` → ffmpeg fps=12 |
| PBR maps | **game-asset-core** — albedo tile + derived normal/rough/height/emissive |
| Aura / trails | Separate VFX sprites for additive Vulkan pass |

### CLI / tools used (what “the commands” are)

There is no separate `/game-asset` shell command. In Grok Build the **skills** are loaded automatically when relevant, and the **tools** are:

| Tool | Use |
|------|-----|
| `image_gen` | New base / tile / VFX from text |
| `image_edit` | Same-character views, maps, variants (always from base) |
| `image_to_video` | Animate a base frame for cycles |
| `ffmpeg` | Harvest frames, resize PBR to 1024 PNG |
| Skills (`game-asset-core`, `game-character-consistency`, `game-animation-frames`) | Quality rules, consistency protocol, video-first animation law |

---

## Runtime wiring (native engine)

| Asset | Path consumed by engine |
|-------|-------------------------|
| Mesh | `assets/characters/bolt/bolt_gsd.glb` |
| Fur PBR | `assets/materials/bolt/bolt_fur_*` (copied from this pack) |
| Aura energy | runtime `BoltAura` + `bolt.frag` (emissive map amplifies) |
| Particles / trails | runtime particle system; FX sprites ready for future atlas |

### Import checklist

1. Hot-reload materials if engine already running (`bolt_fur.json` has `hotReload: true` on engine copy).
2. Sprint keyframes intended **~10–12 fps** playback for 2D billboard fallback; 3D mesh still uses procedural run phase.
3. Aura ring + pawprint: sample as **additive** (`src + dst`) on black.

---

## Animation notes

### Sprint
- **Source video:** `animation/sprint/bolt_sprint_cycle_src.mp4` (6s, side run-in-place)
- **Dense frames:** 73 @ 12 fps (`frame_###.png`)
- **Selected cycle:** 10 keyframes in `cycle/sprint_01.png` … `sprint_10.png`
- **Verify loop:** flip `sprint_10` → `sprint_01` for gait continuity

### Idle (full cycle — done)
- **Source video:** `animation/idle/bolt_idle_cycle_src.mp4` (6s, breathing stand)
- **Seed:** `bolt_idle_stand.jpg`
- **Dense frames:** 73 @ 12 fps
- **Selected cycle:** 8 keys `cycle/idle_01.png` … `idle_08.png` (loop idle_08 → idle_01)

### Jump (full sequence — done)
- **Source video:** `animation/jump/bolt_jump_cycle_src.mp4` (6s, crouch → air → land)
- **Seeds:** `bolt_jump_crouch.jpg`, `bolt_jump_apex.jpg`, `bolt_jump_land.jpg`
- **Dense frames:** 73 @ 12 fps
- **Selected sequence:** 12 keys `cycle/jump_01.png` … `jump_12.png` (play once, or hop loop)

Plain-text guide (opens in Notepad): `animation/README.txt`

---

## Quality targets hit

- [x] Consistent cyan eyes + purple/cyan aura across turnaround  
- [x] Volumetric white fur with cream chest / silver left-flank saddle marker  
- [x] Full turnaround (front / side / back / 3⁄4)  
- [x] Head, paws, tail detail close-ups  
- [x] Sprint side + contact + Star Moss interaction key art  
- [x] PBR: albedo, normal, roughness, height, metallic, emissive @ 1024  
- [x] Sprint cycle source + harvested frames  
- [x] Aura ring + pawprint + crystal dust FX  
- [x] Organized engine-ready tree + identity bible  

## Known limits / next upgrades

- Sprint cycle is **2D harvested** motion reference; 3D `bolt_gsd.glb` still uses engine procedural limb phase (can retarget later).
- Emissive map is a front-pose full-body mask — best for preview; for true 3D use per-UV bake in Blender from this pack as reference.
- Normal map is Imagine-derived (not pure Sobel) — good volume, may need bake polish for final ship.
- Idle breathing cycle / landing frames not fully video-harvested (poses present as seeds).
