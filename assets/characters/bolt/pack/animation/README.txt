Bolt animation pack — how to open / use
========================================

You do NOT need special software for the docs.
Open this file with Notepad (already on Windows).

Videos: double-click the .mp4 files (Windows Movies & TV / VLC / browser).
Frames: double-click any .png (Photos app).


IDLE CYCLE (breathing stand)
----------------------------
Folder:  animation\idle\

  bolt_idle_stand.jpg          seed still (start pose)
  bolt_idle_cycle_src.mp4      6 second source video (720p)
  frame_001.png ... frame_073  dense frames at 12 fps (~73)
  cycle\idle_01.png ... idle_08  selected loopable keyframes

Play intended: ~10-12 fps on the cycle folder (idle_01 -> idle_08 -> idle_01).


JUMP CYCLE (crouch -> air -> land)
----------------------------------
Folder:  animation\jump\

  bolt_jump_crouch.jpg         crouch / anticipation still
  bolt_jump_apex.jpg           mid-air peak still
  bolt_jump_land.jpg           landing still
  bolt_jump_cycle_src.mp4      6 second full jump video
  frame_001.png ... frame_073  dense frames at 12 fps
  cycle\jump_01.png ... jump_12  selected sequence keyframes

Play intended: ~12 fps once (not a loop) OR loop if you only need hop-in-place.


SPRINT CYCLE (already done earlier)
-----------------------------------
Folder:  animation\sprint\

  bolt_sprint_cycle_src.mp4
  frame_001 ... frame_073
  cycle\sprint_01 ... sprint_10


Quick open paths (copy into File Explorer address bar)
------------------------------------------------------
C:\Users\RM\bolt-engine-native\assets\characters\bolt\pack\animation\idle
C:\Users\RM\bolt-engine-native\assets\characters\bolt\pack\animation\jump
C:\Users\RM\bolt-engine-native\assets\characters\bolt\pack\animation\sprint


Note for the 3D game
--------------------
These are 2D reference / billboard / VFX art packs.
The live 3D dog in bolt_crystal.exe still uses code-driven motion on bolt_gsd.glb.
You can use these frames later for sprites or retargeting.
