"""
Build StarBoltSprint white GSD in Blender (headless) and export glTF 2.0.

Usage:
  blender --background --python tools/blender_make_bolt.py -- <out.glb>
"""
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector, Matrix, Euler


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.armatures, bpy.data.images):
        for b in list(block):
            block.remove(b)


def add_material(name, color, rough=0.55, metal=0.0, emission=None, emission_strength=0.0):
    mat = bpy.data.materials.new(name=name)
    mat.use_nodes = True
    nodes = mat.node_tree.nodes
    links = mat.node_tree.links
    nodes.clear()
    out = nodes.new("ShaderNodeOutputMaterial")
    bsdf = nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = rough
    bsdf.inputs["Metallic"].default_value = metal
    if emission is not None and "Emission Color" in bsdf.inputs:
        bsdf.inputs["Emission Color"].default_value = (*emission, 1.0)
        bsdf.inputs["Emission Strength"].default_value = emission_strength
    elif emission is not None and "Emission" in bsdf.inputs:
        bsdf.inputs["Emission"].default_value = (*emission, 1.0)
    links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    return mat


def mesh_obj(name, verts, faces, mat=None, smooth=True):
    mesh = bpy.data.meshes.new(name + "_mesh")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    if mat:
        if obj.data.materials:
            obj.data.materials[0] = mat
        else:
            obj.data.materials.append(mat)
    if smooth:
        for p in mesh.polygons:
            p.use_smooth = True
    return obj


def uv_smart(obj):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode="OBJECT")
    obj.select_set(False)


def make_sphere(name, loc, scale, mat, segs=16):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, location=loc, radius=1.0)
    obj = bpy.context.active_object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if mat:
        obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj


def make_capsule(name, start, end, radius, mat, segs=12):
    """Capsule along start→end as cylinder + 2 hemispheres, joined."""
    s = Vector(start)
    e = Vector(end)
    d = e - s
    length = d.length
    if length < 1e-5:
        return make_sphere(name, start, (radius, radius, radius), mat)
    mid = (s + e) * 0.5
    direction = d.normalized()

    bpy.ops.mesh.primitive_cylinder_add(
        vertices=segs, radius=radius, depth=max(length - 2 * radius * 0.15, radius * 0.5), location=mid
    )
    cyl = bpy.context.active_object
    cyl.name = name + "_cyl"
    # Align +Z of cylinder to direction
    quat = direction.to_track_quat("Z", "Y")
    cyl.rotation_euler = quat.to_euler()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, radius=radius, location=s)
    sph0 = bpy.context.active_object
    sph0.name = name + "_s0"
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, radius=radius, location=e)
    sph1 = bpy.context.active_object
    sph1.name = name + "_s1"
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Join
    bpy.ops.object.select_all(action="DESELECT")
    for o in (cyl, sph0, sph1):
        o.select_set(True)
    bpy.context.view_layer.objects.active = cyl
    bpy.ops.object.join()
    obj = bpy.context.active_object
    obj.name = name
    if mat:
        obj.data.materials.clear()
        obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.use_smooth = True
    return obj


def build_bolt():
    clear_scene()

    fur = add_material("BoltFur", (0.95, 0.96, 0.98), rough=0.55)
    fur_bright = add_material("BoltFurBright", (0.98, 0.99, 1.0), rough=0.48)
    nose = add_material("BoltNose", (0.05, 0.05, 0.07), rough=0.35)
    eye = add_material("BoltEye", (0.4, 0.9, 1.0), rough=0.15, emission=(0.2, 0.8, 1.0), emission_strength=2.5)
    ear_in = add_material("BoltEarIn", (1.0, 0.72, 0.78), rough=0.8)
    pad = add_material("BoltPad", (0.12, 0.13, 0.16), rough=0.85)

    parts = []

    # --- Body (GSD proportions, face +Y in Blender → we'll rotate to +Z for game) ---
    # Blender: Y forward for modeling, export convert to glTF (Y-up, -Z forward often)
    # Engine wants: Y-up, +Z forward (sprint). Export settings handle this.

    # Torso (elongated along Y in Blender = depth)
    torso = make_capsule("Torso", (0, -0.45, 0.95), (0, 0.5, 1.0), 0.30, fur, 16)
    parts.append(torso)

    chest = make_sphere("Chest", (0, 0.55, 0.98), (0.40, 0.45, 0.42), fur_bright, 18)
    parts.append(chest)

    hip = make_sphere("Hip", (0, -0.5, 0.92), (0.36, 0.38, 0.34), fur, 16)
    parts.append(hip)

    belly = make_sphere("Belly", (0, 0.05, 0.72), (0.28, 0.42, 0.22), fur_bright, 14)
    parts.append(belly)

    back = make_sphere("Back", (0, 0.0, 1.12), (0.28, 0.42, 0.22), fur, 14)
    parts.append(back)

    neck = make_capsule("Neck", (0, 0.55, 1.1), (0, 0.85, 1.32), 0.18, fur_bright, 12)
    parts.append(neck)

    # Head
    head = make_sphere("Head", (0, 1.05, 1.38), (0.30, 0.36, 0.28), fur_bright, 18)
    parts.append(head)

    snout = make_capsule("Snout", (0, 1.15, 1.32), (0, 1.48, 1.28), 0.10, fur_bright, 12)
    parts.append(snout)

    nose_obj = make_sphere("Nose", (0, 1.55, 1.28), (0.07, 0.08, 0.06), nose, 12)
    parts.append(nose_obj)

    # Eyes (cyan energy)
    parts.append(make_sphere("EyeL", (0.11, 1.22, 1.42), (0.05, 0.05, 0.05), eye, 12))
    parts.append(make_sphere("EyeR", (-0.11, 1.22, 1.42), (0.05, 0.05, 0.05), eye, 12))
    parts.append(make_sphere("PupilL", (0.11, 1.26, 1.42), (0.022, 0.022, 0.022), nose, 8))
    parts.append(make_sphere("PupilR", (-0.11, 1.26, 1.42), (0.022, 0.022, 0.022), nose, 8))

    # Ears (tall GSD triangles via cones)
    for side, name in ((1, "EarL"), (-1, "EarR")):
        bpy.ops.mesh.primitive_cone_add(
            vertices=6, radius1=0.12, radius2=0.01, depth=0.42, location=(side * 0.14, 1.0, 1.62)
        )
        ear = bpy.context.active_object
        ear.name = name
        ear.rotation_euler = Euler((math.radians(-15), math.radians(side * 12), 0), "XYZ")
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        ear.data.materials.append(fur)
        parts.append(ear)
        # inner
        bpy.ops.mesh.primitive_cone_add(
            vertices=5, radius1=0.07, radius2=0.005, depth=0.32, location=(side * 0.14, 1.02, 1.60)
        )
        ein = bpy.context.active_object
        ein.name = name + "In"
        ein.rotation_euler = Euler((math.radians(-15), math.radians(side * 12), 0), "XYZ")
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
        ein.scale = (0.7, 0.7, 0.85)
        bpy.ops.object.transform_apply(scale=True)
        ein.data.materials.append(ear_in)
        parts.append(ein)

    # Legs
    leg_defs = [
        ("LegFL", (0.18, 0.32, 0.88), (0.16, 0.35, 0.12)),
        ("LegFR", (-0.18, 0.32, 0.88), (-0.16, 0.35, 0.12)),
        ("LegBL", (0.20, -0.42, 0.90), (0.18, -0.40, 0.12)),
        ("LegBR", (-0.20, -0.42, 0.90), (-0.18, -0.40, 0.12)),
    ]
    for name, hip, foot in leg_defs:
        mid = ((hip[0] + foot[0]) * 0.5, (hip[1] + foot[1]) * 0.5, (hip[2] + foot[2]) * 0.5 + 0.05)
        parts.append(make_capsule(name + "U", hip, mid, 0.085, fur, 10))
        parts.append(make_capsule(name + "L", mid, foot, 0.065, fur, 10))
        parts.append(make_sphere(name + "Paw", (foot[0], foot[1] + 0.04, 0.08), (0.10, 0.12, 0.06), pad, 10))

    # Tail
    parts.append(make_capsule("Tail", (0, -0.65, 1.0), (0.05, -1.0, 1.28), 0.06, fur, 10))
    parts.append(make_sphere("TailTip", (0.06, -1.05, 1.32), (0.08, 0.08, 0.08), fur, 10))

    # Join all into one object "Bolt"
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    bolt = bpy.context.active_object
    bolt.name = "Bolt"

    # Origin at feet center
    bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
    # Move so min Z (height in our layout was Z-up for body height...)
    # Our coords: X side, Y forward, Z up (Blender default Z-up)
    # Feet should be at Z=0
    min_z = min((bolt.matrix_world @ Vector(c)).z for c in bolt.bound_box)
    bolt.location.z -= min_z
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

    # Face +Y in Blender; glTF export with +Y up will map; engine wants +Z forward.
    # Rotate -90 around X so +Y (forward) becomes -Z then we'll flip in export...
    # Engine normalizeDogMesh expects mesh; we export with:
    #   export_yup=True, and model facing -Z in glTF convention often.
    # Our engine rotates 180 around Y after load.
    # Keep Blender: +Y = nose direction (forward).

    uv_smart(bolt)

    # Simple armature for future (optional) — export rest pose only for now
    bpy.ops.object.armature_add(enter_editmode=False, location=(0, 0, 0))
    arm = bpy.context.active_object
    arm.name = "BoltArmature"
    # Parent mesh to armature with automatic weights for basic skin
    bpy.ops.object.select_all(action="DESELECT")
    bolt.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    try:
        bpy.ops.object.parent_set(type="ARMATURE_AUTO")
    except Exception:
        # If weights fail, leave mesh alone
        bpy.ops.object.parent_set(type="OBJECT", keep_transform=True)

    return bolt


def export_glb(path: Path):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Blender 4.x glTF exporter
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
        export_skins=True,
        export_morph=False,
    )
    print("EXPORTED", path, "size=", path.stat().st_size if path.exists() else 0)


def main():
    argv = sys.argv
    out = Path("assets/characters/bolt/bolt_gsd.glb")
    if "--" in argv:
        args = argv[argv.index("--") + 1 :]
        if args:
            out = Path(args[0])
    build_bolt()
    export_glb(out.resolve())
    # Also save .blend for re-edit
    blend = out.with_suffix(".blend")
    bpy.ops.wm.save_as_mainfile(filepath=str(blend.resolve()))
    print("SAVED", blend)


if __name__ == "__main__":
    main()
