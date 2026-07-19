"""
Normalize AI 2D→3D mesh (TripoSR/Meshy) for Bolt Engine.

- Feet on ground (Y-up after convert)
- Nose toward +Z (engine forward)
- Height ~2.05m (matches existing GSD scale)
- Optional decimate for low-end GPU
- Export glTF binary

Usage:
  blender --background --python tools/blender_normalize_ai_bolt.py -- \
    assets/characters/bolt/ai3d/triposr_raw.glb \
    assets/characters/bolt/bolt_gsd.glb
"""
from __future__ import annotations

import sys
from pathlib import Path

import bpy
from mathutils import Vector, Matrix
import math


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.images, bpy.data.armatures):
        for b in list(coll):
            coll.remove(b)


def import_mesh(path: Path):
    path = path.resolve()
    suf = path.suffix.lower()
    if suf == ".glb" or suf == ".gltf":
        bpy.ops.import_scene.gltf(filepath=str(path))
    elif suf == ".obj":
        bpy.ops.wm.obj_import(filepath=str(path))
    else:
        raise RuntimeError(f"Unsupported: {path}")
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    if not meshes:
        raise RuntimeError("No mesh in file")
    # Join all mesh objects into one
    bpy.ops.object.select_all(action="DESELECT")
    for o in meshes:
        o.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    return bpy.context.active_object


def world_bounds(obj):
    corners = [obj.matrix_world @ Vector(c) for c in obj.bound_box]
    xs = [c.x for c in corners]
    ys = [c.y for c in corners]
    zs = [c.z for c in corners]
    return (
        Vector((min(xs), min(ys), min(zs))),
        Vector((max(xs), max(ys), max(zs))),
    )


def normalize_bolt(obj, target_height=2.05, max_tris=45000):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Apply all modifiers if any
    for m in list(obj.modifiers):
        try:
            bpy.ops.object.modifier_apply(modifier=m.name)
        except Exception:
            pass

    # Decimate if huge
    me = obj.data
    npoly = len(me.polygons)
    if npoly > max_tris:
        ratio = max(0.08, max_tris / float(npoly))
        d = obj.modifiers.new("Decimate", "DECIMATE")
        d.ratio = ratio
        bpy.ops.object.modifier_apply(modifier="Decimate")
        print(f"Decimated {npoly} -> {len(obj.data.polygons)} polys (ratio={ratio:.3f})")

    # Smooth shade
    for p in obj.data.polygons:
        p.use_smooth = True

    # Recalc normals
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")

    # Scale to target height (Y-up after glTF import in Blender 4 is usually Y-up)
    mn, mx = world_bounds(obj)
    size = mx - mn
    # Detect up axis: tallest dimension
    dims = [(size.x, "x"), (size.y, "y"), (size.z, "z")]
    dims.sort(reverse=True)
    height = dims[0][0]
    if height < 1e-4:
        raise RuntimeError("Degenerate mesh")
    scale = target_height / height
    obj.scale = (scale, scale, scale)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)

    # After glTF import, Blender is usually Y-up. Engine wants Y-up, feet y=0, nose +Z.
    # Many AI meshes face camera (-Z or +Y). Heuristic: longest horizontal = length.
    mn, mx = world_bounds(obj)
    size = mx - mn
    # Center XZ, feet to y=0
    cx = (mn.x + mx.x) * 0.5
    cz = (mn.z + mx.z) * 0.5
    obj.location.x -= cx
    obj.location.z -= cz
    obj.location.y -= mn.y
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

    # If mesh is longer in Z than X, assume already Z-forward-ish; if longer in Y horizontal...
    mn, mx = world_bounds(obj)
    size = mx - mn
    # Rotate so the longest horizontal axis becomes Z (forward)
    # After feet Y=0, horizontal is X and Z
    if size.x > size.z * 1.15:
        # longer on X → rotate -90° around Y so X becomes Z
        obj.rotation_euler = (0, math.radians(-90), 0)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        # re-center
        mn, mx = world_bounds(obj)
        obj.location.x -= (mn.x + mx.x) * 0.5
        obj.location.z -= (mn.z + mx.z) * 0.5
        obj.location.y -= mn.y
        bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)

    # Smart UV if none
    if not obj.data.uv_layers:
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
        bpy.ops.object.mode_set(mode="OBJECT")

    # White fur material (engine also applies PBR)
    mat = bpy.data.materials.new(name="BoltFur")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = (0.92, 0.93, 0.95, 1.0)
        bsdf.inputs["Roughness"].default_value = 0.72
    obj.data.materials.clear()
    obj.data.materials.append(mat)

    mn, mx = world_bounds(obj)
    print(f"Final bounds min={tuple(mn)} max={tuple(mx)} size={tuple(mx-mn)}")
    print(f"Tris≈{len(obj.data.polygons)} verts={len(obj.data.vertices)}")
    return obj


def export_glb(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        use_selection=False,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_materials="EXPORT",
    )
    print("Exported", path, path.stat().st_size)


def main():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1 :]
    else:
        argv = []
    if len(argv) < 2:
        print("Usage: blender --python blender_normalize_ai_bolt.py -- in.glb out.glb")
        sys.exit(1)
    inp = Path(argv[0])
    out = Path(argv[1])
    clear_scene()
    obj = import_mesh(inp)
    normalize_bolt(obj)
    export_glb(out)
    # also save blend
    blend = out.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend))
    print("Saved blend", blend)


if __name__ == "__main__":
    main()
