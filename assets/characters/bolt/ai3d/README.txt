Bolt AI 2D → 3D pipeline (done)
================================
Open with Notepad.

What we ran
-----------
1. Multi-view refs from game-character-consistency pack
2. Free HuggingFace TripoSR Space (image → mesh)
   Source: pack/consistency/turnaround/bolt_turn_front.jpg
3. Blender normalize:
   tools/blender_normalize_ai_bolt.py
   - decimate to ~45k tris
   - height/length scale for engine
   - feet Y=0, smart UV, white fur material
4. Installed as live character:
   assets/characters/bolt/bolt_gsd.glb

Files
-----
  triposr_raw.glb / .obj     raw AI export
  bolt_gsd_triposr.glb       normalized game mesh
  bolt_gsd_triposr.blend     open in Blender to tweak
  ../bolt_gsd.glb            LIVE mesh used by the game
  ../bolt_gsd.glb.prev_fox_backup   previous mesh backup

Re-run generation
-----------------
  python tools/run_triposr_bolt.py

  blender --background --python tools/blender_normalize_ai_bolt.py -- ^
    assets/characters/bolt/ai3d/triposr_raw.glb ^
    assets/characters/bolt/bolt_gsd.glb

Then copy glb to build/assets/characters/bolt/ if needed.

Better quality later (optional)
-------------------------------
  Meshy.ai multi-view (front+side+back+3/4) often beats single-image TripoSR.
  Upload pack/consistency/turnaround/*.jpg → export GLB → same Blender normalize script.

Notes
-----
  - Still one solid mesh (no skeleton) → engine Path 2 code anim still applies
  - Fur PBR remains: assets/materials/bolt/bolt_fur_*
  - If orientation looks wrong, open bolt_gsd_triposr.blend and rotate, re-export

Live choice: bolt_gsd_triposr.glb (front view) � better standing height than 3q.
Alt: bolt_gsd_triposr_3q.glb if you prefer the three-quarter silhouette.

