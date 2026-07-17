Drop Grok Imagine PNG/JPG exports here, then run:

  scripts\run_grok_pipeline.ps1

Or import one material:

  build\bolt_grok_import.exe --in assets/grok_inbox/crystal_ground_src.png ^
    --out assets/materials/crystal_nebula --name crystal_ground

Crystal Nebula pack (same biome, multiple surfaces):
  crystal_ground_src.png  — seamless top-down moss/quartz ground
  crystal_path_src.png    — seamless worn path surface (no baked stripe)
  crystal_rock_src.png    — seamless crystalline rock for slopes
  crystal_stalk_src.png   — seamless vertical crystal bark for stalks

Prefer seamless / tileable prompts; keep one palette by editing from ground ref.
