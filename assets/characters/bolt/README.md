# StarBoltSprint — Bolt (Blender character)

## Portable Blender = full Blender

The zip under `tools/blender-portable/` is the **same Blender** as the installer — no admin, no worse quality.

## Build Bolt mesh

```powershell
$blender = "tools\blender-portable\blender-4.2.16-windows-x64\blender.exe"
& $blender --background --python tools\blender_make_bolt.py -- assets\characters\bolt\bolt_gsd.glb
```

Outputs:
- `bolt_gsd.glb` — runtime mesh (engine loads this)
- `bolt_gsd.blend` — re-open in Blender to edit

## Engine pipeline

1. Blender models low-poly white GSD-style dog + UVs + materials  
2. Export glTF 2.0 (`.glb`)  
3. Engine `loadGltfMesh` → normalize size/feet/+Z  
4. Runtime white fur PBR: `assets/materials/bolt/bolt_fur_*`  
5. Aura + orbit camera in Vulkan  

## Controls

- **RMB drag** — orbit camera  
- **Scroll / +/-** — zoom  
- **WASD + Shift** — move / sprint  
