# StarBoltSprint — White German Shepherd

## Pipeline (same as crystal terrain)

| Step | Bolt |
|------|------|
| **Geometry** | Multi-part mid-poly GSD (body, 4 legs, tail, aura) — procedural, or override body via OBJ |
| **Imagine** | Seamless fur → `bolt_fur_*` PBR |
| **Vulkan** | UV + triplanar fur on parts; cyan eyes; lightning aura shell |
| **Animation** | 4-phase diagonal run hop (legs) + tail wag + body bob |

## Artist override

Place `bolt_gsd.obj` here to replace the **body** mesh (legs/tail/aura stay procedural for animation):

```
assets/characters/bolt/bolt_gsd.obj
```

`usemtl eye|nose|ear|pad|aura|fur` tags set material regions.

Generated reference: `bolt_gsd_generated.obj` (body export).

## Runtime features

- Fur PBR (albedo/normal/roughness) from Grok import  
- Run cycle driven by speed  
- Aura + eye emissive scale with **Sprint Score / momentum**  
- Soft dust trail (existing particles)  
