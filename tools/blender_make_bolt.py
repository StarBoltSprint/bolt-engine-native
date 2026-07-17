"""
StarBoltSprint — high-quality stylized white GSD in Blender → glTF.

Much better than sphere-stack: voxel remesh for solid silhouette,
GSD proportions (deep chest, wedge head, tall ears, long legs),
clean UVs, cyan energy eyes, pure white fur materials.

Usage:
  blender --background --python tools/blender_make_bolt.py -- assets/characters/bolt/bolt_gsd.glb
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector, Euler, Matrix


# ---------------------------------------------------------------------------
# Scene helpers
# ---------------------------------------------------------------------------

def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for coll in (bpy.data.meshes, bpy.data.materials, bpy.data.armatures,
                 bpy.data.images, bpy.data.curves):
        for b in list(coll):
            coll.remove(b)


def new_mat(name, base, rough=0.5, metal=0.0, emit=None, emit_str=0.0):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*base, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    # Blender 4.x emission
    if "Emission Color" in bsdf.inputs and emit is not None:
        bsdf.inputs["Emission Color"].default_value = (*emit, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emit_str
    elif "Emission" in bsdf.inputs and emit is not None:
        bsdf.inputs["Emission"].default_value = (*emit, 1.0)
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def apply_mat(obj, mat):
    obj.data.materials.clear()
    obj.data.materials.append(mat)


def smooth(obj):
    for p in obj.data.polygons:
        p.use_smooth = True
    try:
        mod = obj.modifiers.new("Smooth", "SMOOTH")
        mod.factor = 0.5
        mod.iterations = 2
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier="Smooth")
    except Exception:
        pass


def make_uv_sphere(name, loc, scale, segs=24):
    bpy.ops.mesh.primitive_uv_sphere_add(
        segments=segs, ring_count=max(8, segs // 2), radius=1.0, location=loc
    )
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def make_cylinder(name, loc, radius, depth, segs=16, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cylinder_add(
        vertices=segs, radius=radius, depth=depth, location=loc
    )
    o = bpy.context.active_object
    o.name = name
    o.rotation_euler = Euler(rot, "XYZ")
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


def make_cone(name, loc, r1, r2, depth, segs=8, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cone_add(
        vertices=segs, radius1=r1, radius2=r2, depth=depth, location=loc
    )
    o = bpy.context.active_object
    o.name = name
    o.rotation_euler = Euler(rot, "XYZ")
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    return o


def make_cube(name, loc, scale):
    bpy.ops.mesh.primitive_cube_add(size=2.0, location=loc)
    o = bpy.context.active_object
    o.name = name
    o.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    return o


def join_objects(objects, name):
    bpy.ops.object.select_all(action="DESELECT")
    for o in objects:
        if o is not None:
            o.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    o = bpy.context.active_object
    o.name = name
    return o


def remesh_clean(obj, voxel=0.035, decimate=0.35):
    """Solid low-poly silhouette via voxel remesh + decimate."""
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)

    # Voxel remesh merges all blobs into one clean shell
    m = obj.modifiers.new("Remesh", "REMESH")
    m.mode = "VOXEL"
    m.voxel_size = voxel
    m.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier="Remesh")

    # Controlled poly count
    d = obj.modifiers.new("Decimate", "DECIMATE")
    d.ratio = decimate
    bpy.ops.object.modifier_apply(modifier="Decimate")

    # Recalc normals
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    smooth(obj)
    return obj


def smart_uv(obj):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.03)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.select_set(False)


def feet_to_ground(obj):
    """Origin at ground center under feet (Z-up)."""
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    zs = [(obj.matrix_world @ Vector(c)).z for c in obj.bound_box]
    min_z = min(zs)
    obj.location.z -= min_z
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)
    # Origin to geometry bottom center
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
    zs = [(obj.matrix_world @ Vector(c)).z for c in obj.bound_box]
    min_z = min(zs)
    obj.location.z -= min_z
    bpy.ops.object.transform_apply(location=True, rotation=False, scale=False)


# ---------------------------------------------------------------------------
# GSD construction (Blender Z-up, +Y = nose / forward)
# ---------------------------------------------------------------------------

def build_gsd_body(fur, fur_b):
    """Deep chest, athletic torso, sloping back — classic GSD read."""
    parts = []

    # Chest (deep, powerful)
    parts.append(make_uv_sphere("Chest", (0, 0.35, 0.95), (0.38, 0.48, 0.42), 28))
    apply_mat(parts[-1], fur_b)

    # Ribcage / mid torso
    parts.append(make_uv_sphere("Ribs", (0, 0.0, 0.92), (0.34, 0.55, 0.36), 24))
    apply_mat(parts[-1], fur)

    # Loin / waist (slightly tucked)
    parts.append(make_uv_sphere("Loin", (0, -0.35, 0.95), (0.30, 0.40, 0.32), 20))
    apply_mat(parts[-1], fur)

    # Croup / hip (GSD rear angulation mass)
    parts.append(make_uv_sphere("Croup", (0, -0.62, 0.98), (0.34, 0.36, 0.34), 22))
    apply_mat(parts[-1], fur)

    # Belly
    parts.append(make_uv_sphere("Belly", (0, 0.05, 0.68), (0.26, 0.50, 0.20), 18))
    apply_mat(parts[-1], fur_b)

    # Withers / shoulder top
    parts.append(make_uv_sphere("Withers", (0, 0.25, 1.18), (0.28, 0.32, 0.16), 16))
    apply_mat(parts[-1], fur)

    # Neck ruff (thick GSD neck)
    for i, y in enumerate((0.55, 0.72, 0.88)):
        s = 0.22 - i * 0.02
        p = make_uv_sphere(f"Neck{i}", (0, y, 1.15 + i * 0.06), (s * 1.15, s * 1.1, s), 16)
        apply_mat(p, fur_b if i % 2 == 0 else fur)
        parts.append(p)

    return join_objects(parts, "BodyCore")


def build_gsd_head(fur, fur_b, nose_m, eye_m):
    parts = []

    # Skull (wedge)
    skull = make_uv_sphere("Skull", (0, 1.05, 1.42), (0.28, 0.34, 0.26), 26)
    apply_mat(skull, fur_b)
    parts.append(skull)

    # Stop / brow ridge
    brow = make_cube("Brow", (0, 1.12, 1.52), (0.22, 0.10, 0.05))
    apply_mat(brow, fur)
    parts.append(brow)

    # Muzzle (long GSD)
    muzzle = make_uv_sphere("Muzzle", (0, 1.32, 1.32), (0.14, 0.28, 0.12), 18)
    apply_mat(muzzle, fur_b)
    parts.append(muzzle)

    # Bridge
    bridge = make_cube("Bridge", (0, 1.28, 1.40), (0.08, 0.18, 0.05))
    apply_mat(bridge, fur)
    parts.append(bridge)

    # Cheeks
    for side in (1, -1):
        ch = make_uv_sphere(f"Cheek{side}", (side * 0.16, 1.12, 1.32), (0.12, 0.14, 0.12), 14)
        apply_mat(ch, fur_b)
        parts.append(ch)

    # Nose leather
    nose = make_uv_sphere("Nose", (0, 1.55, 1.30), (0.07, 0.08, 0.055), 14)
    apply_mat(nose, nose_m)
    parts.append(nose)

    # Eyes — cyan energy, set slightly into skull
    for side, nm in ((1, "EyeL"), (-1, "EyeR")):
        e = make_uv_sphere(nm, (side * 0.12, 1.20, 1.45), (0.045, 0.04, 0.04), 12)
        apply_mat(e, eye_m)
        parts.append(e)
        p = make_uv_sphere(nm + "P", (side * 0.12, 1.235, 1.45), (0.02, 0.018, 0.018), 8)
        apply_mat(p, nose_m)
        parts.append(p)

    # Jaw
    jaw = make_uv_sphere("Jaw", (0, 1.22, 1.20), (0.12, 0.20, 0.10), 14)
    apply_mat(jaw, fur)
    parts.append(jaw)

    return join_objects(parts, "HeadCore")


def build_gsd_ears(fur, ear_in):
    parts = []
    for side, name in ((1, "EarL"), (-1, "EarR")):
        # Outer ear — tall pointed (GSD erect)
        ear = make_cone(
            name,
            (side * 0.16, 1.02, 1.72),
            0.11,
            0.008,
            0.48,
            segs=7,
            rot=(math.radians(-8), math.radians(side * 8), math.radians(side * 5)),
        )
        apply_mat(ear, fur)
        parts.append(ear)
        # Inner pink
        inn = make_cone(
            name + "In",
            (side * 0.16, 1.04, 1.68),
            0.065,
            0.004,
            0.36,
            segs=6,
            rot=(math.radians(-8), math.radians(side * 8), math.radians(side * 5)),
        )
        apply_mat(inn, ear_in)
        parts.append(inn)
    return parts


def build_gsd_legs(fur, pad_m):
    """Long athletic legs with clear upper/lower segments."""
    parts = []
    # (name, hip_xyz, knee_xyz, foot_xyz)
    legs = [
        ("FL", (0.20, 0.28, 0.85), (0.20, 0.30, 0.48), (0.20, 0.32, 0.08)),
        ("FR", (-0.20, 0.28, 0.85), (-0.20, 0.30, 0.48), (-0.20, 0.32, 0.08)),
        ("BL", (0.22, -0.48, 0.90), (0.22, -0.42, 0.50), (0.20, -0.38, 0.08)),
        ("BR", (-0.22, -0.48, 0.90), (-0.22, -0.42, 0.50), (-0.20, -0.38, 0.08)),
    ]
    for name, hip, knee, foot in legs:
        # Upper
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.09, depth=1.0, location=(0, 0, 0))
        u = bpy.context.active_object
        u.name = f"Leg{name}U"
        # Position/orient cylinder from hip to knee
        h, k = Vector(hip), Vector(knee)
        mid = (h + k) * 0.5
        d = k - h
        u.location = mid
        u.scale = (1, 1, d.length)
        u.rotation_mode = "QUATERNION"
        u.rotation_quaternion = d.normalized().to_track_quat("Z", "Y")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        apply_mat(u, fur)
        parts.append(u)

        # Lower
        bpy.ops.mesh.primitive_cylinder_add(vertices=12, radius=0.07, depth=1.0, location=(0, 0, 0))
        lo = bpy.context.active_object
        lo.name = f"Leg{name}L"
        h, k = Vector(knee), Vector(foot)
        mid = (h + k) * 0.5
        d = k - h
        lo.location = mid
        lo.scale = (1, 1, d.length)
        lo.rotation_mode = "QUATERNION"
        lo.rotation_quaternion = d.normalized().to_track_quat("Z", "Y")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        apply_mat(lo, fur)
        parts.append(lo)

        # Paw
        paw = make_uv_sphere(f"Paw{name}", (foot[0], foot[1] + 0.05, 0.07), (0.10, 0.13, 0.055), 12)
        apply_mat(paw, pad_m)
        parts.append(paw)

        # Shoulder / thigh mass
        if name.startswith("F"):
            sh = make_uv_sphere(f"Shoulder{name}", hip, (0.14, 0.14, 0.16), 12)
        else:
            sh = make_uv_sphere(f"Thigh{name}", hip, (0.16, 0.15, 0.16), 12)
        apply_mat(sh, fur)
        parts.append(sh)

    return parts


def build_gsd_tail(fur):
    parts = []
    # GSD tail — long, slightly raised saber
    pts = [
        (0, -0.72, 1.0),
        (0.02, -0.95, 1.15),
        (0.04, -1.15, 1.28),
        (0.05, -1.32, 1.22),
    ]
    for i in range(len(pts) - 1):
        a, b = Vector(pts[i]), Vector(pts[i + 1])
        mid = (a + b) * 0.5
        d = b - a
        bpy.ops.mesh.primitive_cylinder_add(vertices=10, radius=0.055 - i * 0.008, depth=1.0, location=(0, 0, 0))
        c = bpy.context.active_object
        c.name = f"Tail{i}"
        c.location = mid
        c.scale = (1, 1, d.length)
        c.rotation_mode = "QUATERNION"
        c.rotation_quaternion = d.normalized().to_track_quat("Z", "Y")
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        apply_mat(c, fur)
        parts.append(c)
    tip = make_uv_sphere("TailTip", pts[-1], (0.07, 0.08, 0.07), 10)
    apply_mat(tip, fur)
    parts.append(tip)
    return parts


def build_bolt():
    clear_scene()

    # Pure white GSD palette
    fur = new_mat("BoltFur", (0.94, 0.95, 0.97), rough=0.52)
    fur_b = new_mat("BoltFurBright", (0.98, 0.99, 1.0), rough=0.42)
    nose_m = new_mat("BoltNose", (0.04, 0.04, 0.05), rough=0.32)
    eye_m = new_mat(
        "BoltEye",
        (0.35, 0.85, 1.0),
        rough=0.12,
        emit=(0.15, 0.75, 1.0),
        emit_str=3.5,
    )
    ear_in = new_mat("BoltEarIn", (1.0, 0.70, 0.76), rough=0.78)
    pad_m = new_mat("BoltPad", (0.10, 0.11, 0.13), rough=0.88)

    body = build_gsd_body(fur, fur_b)
    head = build_gsd_head(fur, fur_b, nose_m, eye_m)
    ears = build_gsd_ears(fur, ear_in)
    legs = build_gsd_legs(fur, pad_m)
    tail = build_gsd_tail(fur)

    # Keep eyes out of remesh so they stay sharp cyan spheres
    eye_objs = [o for o in bpy.data.objects if o.name.startswith("Eye") or o.name.startswith("Pupil")]
    for o in eye_objs:
        o.select_set(False)

    # Remesh body parts (not eyes) into cohesive low-poly shell
    remesh_targets = [body, head] + ears + legs + tail
    remesh_targets = [o for o in remesh_targets if o is not None and o.name not in [e.name for e in eye_objs]]

    body_join = join_objects(remesh_targets, "BoltBody")
    # Finer remesh for readable silhouette (~2–4k tris after decimate)
    remesh_clean(body_join, voxel=0.028, decimate=0.42)
    apply_mat(body_join, fur)
    smart_uv(body_join)

    # Re-add eyes on top (separate so emissive survives)
    eye_parts = []
    for side, nm in ((1, "EyeL"), (-1, "EyeR")):
        e = make_uv_sphere(nm, (side * 0.12, 1.20, 1.45), (0.048, 0.042, 0.042), 14)
        apply_mat(e, eye_m)
        eye_parts.append(e)
        p = make_uv_sphere(nm + "P", (side * 0.12, 1.24, 1.45), (0.02, 0.018, 0.018), 10)
        apply_mat(p, nose_m)
        eye_parts.append(p)

    # Final join (eyes + body)
    bolt = join_objects([body_join] + eye_parts, "Bolt")
    feet_to_ground(bolt)

    # Scale to ~1.1 Blender units tall (engine normalizes to ~2m)
    # Ensure reasonable size
    dims = bolt.dimensions
    if dims.z > 0.01:
        target = 1.15  # Blender units height
        s = target / dims.z
        bolt.scale = (s, s, s)
        bpy.ops.object.transform_apply(scale=True)
        feet_to_ground(bolt)

    # Face +Y (nose forward). Engine glTF loader may 180-yaw; OK.
    bolt.rotation_euler = (0, 0, 0)
    bpy.ops.object.transform_apply(rotation=True)

    # Stats
    tris = sum(len(p.vertices) - 2 for p in bolt.data.polygons if len(p.vertices) >= 3)
    print(f"BOLT MESH: verts={len(bolt.data.vertices)} polys={len(bolt.data.polygons)} ~tris={tris}")
    print(f"BOLT DIMS: {tuple(bolt.dimensions)}")
    return bolt


def export_glb(path: Path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(
        filepath=str(path),
        export_format="GLB",
        use_selection=False,
        export_apply=True,
        export_texcoords=True,
        export_normals=True,
        export_materials="EXPORT",
        export_yup=True,
        export_animations=False,
        export_skins=False,
        export_morph=False,
        export_nla_strips=False,
    )
    print("EXPORTED", path, "bytes=", path.stat().st_size if path.exists() else 0)


def main():
    out = Path("assets/characters/bolt/bolt_gsd.glb")
    argv = sys.argv
    if "--" in argv:
        args = argv[argv.index("--") + 1 :]
        if args:
            out = Path(args[0])
    build_bolt()
    export_glb(out.resolve())
    blend = out.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend.resolve()))
    print("SAVED", blend)


if __name__ == "__main__":
    main()
