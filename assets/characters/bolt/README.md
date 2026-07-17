# StarBoltSprint — White German Shepherd / quadruped

## Pipeline (same as crystal terrain)

| Step | Bolt |
|------|------|
| **Geometry** | **Imported low-poly mesh** (`bolt_gsd.glb` / `.obj`) preferred; procedural multi-part fallback |
| **Imagine** | Seamless pure-white fur → `bolt_fur_*` PBR |
| **Vulkan** | UV + triplanar fur on mesh; lightning aura shell |
| **Animation** | Body bob / lean / stretch; multi-part leg hop if procedural |

## Default imported mesh

`bolt_gsd.glb` — free **CC-BY 4.0** low-poly fox (PixelMannen / Khronos sample), restyled with white fur PBR as Bolt stand-in.

See `ATTRIBUTION.txt`. Swap anytime with a real German Shepherd:

```
assets/characters/bolt/bolt_gsd.glb   # or .obj / .gltf
```

Loader auto-fits: feet on ground, nose toward +Z, height ~1.35 m.

## Runtime

- Fur PBR from Grok import  
- Sprint bob + aura intensity from score/momentum  
- Dust trail particles  

