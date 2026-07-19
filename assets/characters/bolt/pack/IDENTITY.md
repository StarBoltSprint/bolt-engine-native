# Bolt — Character Identity Bible (HQ Pack)

**Role:** Protagonist of Bolt Engine / Crystal Nebula Plains  
**Species:** Majestic white German Shepherd  
**Personality:** Joyful, determined, slightly majestic

## Locked visual identity

| Trait | Spec |
|-------|------|
| Fur | Dense volumetric white; cream chest ruff; cool blue-shadow underbelly |
| Saddle | Faint silver-gray wash, **stronger on character's LEFT flank** |
| Eyes | Luminous **cyan-blue glow**, intelligent / joyful |
| Nose | Black, slightly wet |
| Ears | Upright GSD points, soft pink interiors |
| Aura | Elegant **purple + cyan** cosmic lightning along shoulders/spine — refined, not neon chaos |
| Tail | Feathered white plume |
| Build | Athletic adult GSD, powerful but elegant |
| Style | High-end stylized 3D game character / semi-realistic fur |
| Background (2D pack) | Flat solid `#1a1528` deep indigo (keyable) |

## Asymmetry map (turnarounds)

| View | Silver saddle | Aura density | Character left appears… |
|------|---------------|--------------|-------------------------|
| Front | stronger on viewer's RIGHT | soft bilateral arcs | viewer's RIGHT |
| Right profile (nose→right edge) | near side = character right = **lighter** saddle | arcs along back ridge | far side |
| Back | stronger on viewer's LEFT | spine lightning readable | viewer's LEFT |
| 3/4 front-left | strong silver on near flank | cyan sparks near shoulder | near |

## Engine paths

| Asset | Path |
|-------|------|
| HQ pack root | `assets/characters/bolt/pack_hq/` |
| Live PBR (engine load) | `assets/materials/bolt/bolt_fur_*` |
| 3D mesh (existing) | `assets/characters/bolt/bolt_gsd.glb` |
| Runtime aura | `BoltAura` + `bolt.frag` energy; emissive map amplifies |
