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

1. **AI 2D→3D (current live):** multi-view Imagine refs → HuggingFace TripoSR → Blender normalize  
   - See `ai3d/README.txt` and `tools/run_triposr_bolt.py`  
   - Live mesh: `bolt_gsd.glb` (from `ai3d/bolt_gsd_triposr.glb`)  
2. Or Blender sculpt: `tools/blender_make_bolt.py`  
3. Engine `loadGltfMesh` → normalize size/feet/+Z  
4. Runtime white fur PBR: `assets/materials/bolt/bolt_fur_*`  
5. Aura + Path 2 idle/run/jump + orbit camera in Vulkan  

## Controls

- **RMB drag** — orbit camera  
- **Scroll / +/-** — zoom  
- **WASD + Shift** — move / sprint  
