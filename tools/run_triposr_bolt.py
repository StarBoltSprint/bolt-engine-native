from pathlib import Path
from gradio_client import Client, handle_file
import shutil
import traceback

ROOT = Path(r"C:\Users\RM\bolt-engine-native")
PACK = ROOT / "assets" / "characters" / "bolt" / "pack"
OUT = ROOT / "assets" / "characters" / "bolt" / "ai3d"
OUT.mkdir(parents=True, exist_ok=True)

# Best single image for TripoSR: clear front, full body, flat bg
src = PACK / "consistency" / "turnaround" / "bolt_turn_front.jpg"
if not src.exists():
    src = PACK / "source" / "bolt_base.jpg"
print("SOURCE:", src)

client = Client("stabilityai/TripoSR")
print("Connected")

print("Preprocess...")
processed = client.predict(
    handle_file(str(src)),
    True,   # remove_background
    0.85,   # foreground_ratio
    api_name="/preprocess",
)
print("Processed:", processed)

print("Generate mesh (this can take 1-3 min on HF free GPU)...")
obj_path, glb_path = client.predict(
    handle_file(processed),
    256,  # marching cubes resolution
    api_name="/generate",
)
print("OBJ:", obj_path)
print("GLB:", glb_path)

for label, p in (("triposr_raw.obj", obj_path), ("triposr_raw.glb", glb_path)):
    if p and Path(p).exists():
        dest = OUT / label
        shutil.copy2(p, dest)
        print("Saved", dest, dest.stat().st_size)
    else:
        print("Missing", label, p)

print("DONE")
