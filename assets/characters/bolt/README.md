# StarBoltSprint — Pure White German Shepherd

## Correct pipeline (same as crystal terrain)

| Step | Bolt |
|------|------|
| **Geometry** | Procedural 3D GSD mesh (`buildBoltMesh`) — body, head, legs, tail, ears |
| **Imagine** | Seamless pure-white fur tile → `bolt_fur_src` |
| **Offline** | `bolt_grok_import` → `assets/materials/bolt/bolt_fur_*` PBR |
| **Vulkan** | Triplanar fur albedo/normal/roughness on mesh + cyan energy eyes |

**Not** a 2D billboard. Character art turnarounds (`bolt_base.jpg` etc.) are identity reference only.

## Runtime

```
loadBoltFurPBR("assets/materials/bolt/bolt_fur")
uploadBoltMesh(buildBoltMesh(...))
```

Material regions on mesh (UV.x): fur | eye/energy | nose | ear-inner | pad
