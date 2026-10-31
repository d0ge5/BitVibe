"""Renders BitVibe's BEAT DOG: the green wireframe husky head headbanging through one beat, as a sprite sheet (32 frames,
8 across) that the plug-in shows in time with the tempo. The model is cut to the head, drawn as a glowing wireframe over
a black fill, and rigged (head, two ears).

  blender -b --factory-startup --python build_beat_dog.py -- <husky.glb> <sheet.jpg> [<BeatDogMesh.h>]

With a third path it also writes the head's mesh, weights, rig, camera and the motion curves as a C++ header, which the
plug-in's live (real-time) BEAT DOG draws from, so both versions show exactly the same dog and the same headbang."""
import bpy, bmesh, json, math, os, sys
from mathutils import Vector, Quaternion, Matrix

import tempfile
SRC = ""
OUT_DIR = os.path.join(tempfile.gettempdir(), "bitvibe_beat_dog")  # the frames and a .blend to look at
os.makedirs(OUT_DIR, exist_ok=True)
ARGS = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
SRC = ARGS[0] if len(ARGS) > 0 else SRC                 # the husky model
SHEET = ARGS[1] if len(ARGS) > 1 else os.path.join(OUT_DIR, "beat_dog.jpg")
HEADER = ARGS[2] if len(ARGS) > 2 else ""               # also write the mesh, rig and motion for the live version

FRAMES, COLS = 32, 8   # frames in one beat, laid out 8 across
TILE = 224             # pixels per frame
GREEN = (0.02, 1.0, 0.10, 1.0)  # terminal green

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
bpy.ops.import_scene.gltf(filepath=SRC)
src = [o for o in scene.objects if o.type == 'MESH'][0]
src.name = "HuskyHead"
src.data.name = "HuskyHeadMesh"

# ---- the head: forward of the neck, above the shoulders, cut on a slant so no neck flaps remain
bm = bmesh.new()
bm.from_mesh(src.data)
gone = [v for v in bm.verts if v.co.x > -0.20 or v.co.z < 0.56 or v.co.z < 0.56 + 1.5 * (v.co.x + 0.30)]
bmesh.ops.delete(bm, geom=gone, context='VERTS')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')  # loose leftovers
# keep only the head itself: the teeth are tiny separate pieces that turn into tangled clumps in a wireframe
bm.verts.ensure_lookup_table()
islands, seen = [], set()
for v in bm.verts:
    if v in seen:
        continue
    part, stack = [], [v]
    seen.add(v)
    while stack:
        cur = stack.pop()
        part.append(cur)
        for e in cur.link_edges:
            o = e.other_vert(cur)
            if o not in seen:
                seen.add(o)
                stack.append(o)
    islands.append(part)
islands.sort(key=len, reverse=True)
removed_pieces = len(islands) - 1
bmesh.ops.delete(bm, geom=[v for part in islands[1:] for v in part], context='VERTS')
# and the needle-thin triangles left where the teeth joined the jaw (they show as spikes in the wireframe)
def thin(f):
    longest = max(e.calc_length() for e in f.edges)
    return f.calc_area() < 2.0e-4 and longest * longest / max(f.calc_area(), 1e-12) > 30.0
slivers = [f for f in bm.faces if thin(f)]
removed_slivers = len(slivers)
bmesh.ops.delete(bm, geom=slivers, context='FACES_ONLY')
bmesh.ops.delete(bm, geom=[e for e in bm.edges if not e.link_faces], context='EDGES')
bmesh.ops.delete(bm, geom=[v for v in bm.verts if not v.link_faces], context='VERTS')
bm.to_mesh(src.data)
bm.free()
for p in src.data.polygons:
    p.use_smooth = False

verts = [v.co.copy() for v in src.data.vertices]
lo = Vector((min(v.x for v in verts), min(v.y for v in verts), min(v.z for v in verts)))
hi = Vector((max(v.x for v in verts), max(v.y for v in verts), max(v.z for v in verts)))
centre = (lo + hi) / 2

# the two ear tips: the highest vertex, then the highest one well away from it
tips = sorted(verts, key=lambda v: -v.z)
tipA = tips[0]
tipB = next(v for v in tips if (Vector((v.x, v.y)) - Vector((tipA.x, tipA.y))).length > 0.06)
EAR_BASE_Z = 0.79
SNOUT = Vector((-0.33, -0.94, 0.0)).normalized()        # which way the dog faces
PITCH_AXIS = SNOUT.cross(Vector((0, 0, 1))).normalized()  # nodding axis
# outward is away from the other ear
ears = {"ear.A": tipA, "ear.B": tipB}

# ---- the rig
arm_data = bpy.data.armatures.new("HuskyRig")
rig = bpy.data.objects.new("HuskyRig", arm_data)
scene.collection.objects.link(rig)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode='EDIT')
neck_root = Vector((-0.29, 0.03, 0.60))
head_b = arm_data.edit_bones.new("head")
head_b.head = neck_root
head_b.tail = neck_root + Vector((0, 0, 0.18))
for name, tip in ears.items():
    b = arm_data.edit_bones.new(name)
    b.head = Vector((tip.x, tip.y, EAR_BASE_Z))
    b.tail = Vector((tip.x, tip.y, tip.z))
    b.parent = head_b
bpy.ops.object.mode_set(mode='OBJECT')

# weights: everything follows the head; ear vertices follow their ear more the higher they are
for g in ("head", "ear.A", "ear.B"):
    src.vertex_groups.new(name=g)
for v in src.data.vertices:
    co = v.co
    w_ear, which = 0.0, None
    for name, tip in ears.items():
        d = (Vector((co.x, co.y)) - Vector((tip.x, tip.y))).length
        if d < 0.07 and co.z > EAR_BASE_Z - 0.02:
            t = max(0.0, min(1.0, (co.z - (EAR_BASE_Z - 0.02)) / (tip.z - EAR_BASE_Z + 0.02)))
            w = t * t * (3 - 2 * t) * max(0.0, 1.0 - d / 0.07) ** 0.5
            if w > w_ear:
                w_ear, which = w, name
    src.vertex_groups["head"].add([v.index], 1.0 - w_ear, 'REPLACE')
    if which:
        src.vertex_groups[which].add([v.index], w_ear, 'REPLACE')

src.parent = rig
arm_mod = src.modifiers.new("Rig", 'ARMATURE')
arm_mod.object = rig

# ---- the look: the mesh drawn black (it hides the far side's lines), a glowing green wireframe over it
def emission_material(name, colour, strength):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = colour
    em.inputs["Strength"].default_value = strength
    nt.links.new(em.outputs[0], out.inputs[0])
    return m

fill_mat = emission_material("HackerFill", (0.0, 0.012, 0.004, 1.0), 1.0)
wire_mat = emission_material("HackerGreen", GREEN, 2.6)
src.data.materials.clear()
src.data.materials.append(fill_mat)

wire = src.copy()
wire.data = src.data.copy()  # its own copy of the mesh (same shape and weights), with only the green material
wire.data.name = "HuskyHeadWireMesh"
wire.data.materials.clear()
wire.data.materials.append(wire_mat)
wire.name = "HuskyHeadWire"
scene.collection.objects.link(wire)
wire.parent = rig
wire.modifiers["Rig"].object = rig
wf = wire.modifiers.new("Wire", 'WIREFRAME')
wf.thickness = 0.0027
wf.offset = 0.7            # the lines sit mostly outside the surface, so the black fill does not swallow them
wf.use_even_offset = True
wf.use_replace = True
wf.material_offset = 0

# ---- the animation: one beat of headbanging. The head hits bottom exactly on the beat (frame 1), springs a little
# further down, rises slowly, then slams down again, faster and faster, into the next beat. The ears are a damped
# spring that follows the head, so they lag behind, keep going when the head stops, and flop back.
for pb in rig.pose.bones:
    pb.rotation_mode = 'QUATERNION'

def local_rotation(pb, axis_world, angle):
    rest = (rig.matrix_world @ pb.bone.matrix_local).to_quaternion()
    q_world = Quaternion(axis_world, angle)
    return rest.inverted() @ q_world @ rest

DOWN, UP = math.radians(24.0), math.radians(-9.0)
def head_angle(p):
    """Head pitch (down positive) at phase p of the beat, 0 = the beat."""
    p %= 1.0
    rise_end = 0.6
    if p < rise_end:  # off the hit: quick at first, easing into the top
        u = p / rise_end
        a = DOWN + (UP - DOWN) * (1 - (1 - u) ** 3)
    else:             # the slam: slow to start, accelerating into the beat
        u = (p - rise_end) / (1 - rise_end)
        a = UP + (DOWN - UP) * u ** 3.2
    if p < 0.14:      # the impact carries it a little further down
        a += math.radians(5.0) * math.sin(math.pi * p / 0.14)
    return a

# the ears: a damped spring after the head, run for several beats so the motion has settled into its loop
STEPS = FRAMES * 40
k, c = 1400.0, 26.0          # stiffness and damping, per beat (time unit = one beat)
theta, vel = head_angle(0.0), 0.0
ear_curve = []
for cycle in range(6):
    for s in range(STEPS):
        p = s / STEPS
        dt = 1.0 / STEPS
        acc = k * (head_angle(p) - theta) - c * vel
        vel += acc * dt
        theta += vel * dt
        if cycle == 5:
            ear_curve.append(theta - head_angle(p))

scene.frame_start = 1
scene.frame_end = FRAMES
head_pb = rig.pose.bones["head"]
for f in range(1, FRAMES + 1):
    p = (f - 1) / FRAMES
    a = head_angle(p)
    head_pb.rotation_quaternion = local_rotation(head_pb, PITCH_AXIS, a)
    head_pb.location = (0.0, -0.012 * max(0.0, a) / DOWN, 0.0)  # the whole head dips with the hit
    head_pb.keyframe_insert("rotation_quaternion", frame=f)
    head_pb.keyframe_insert("location", frame=f)
    rel = ear_curve[int(p * STEPS) % STEPS] * 1.6
    for i, name in enumerate(("ear.A", "ear.B")):
        pb = rig.pose.bones[name]
        side = math.radians(5.0) * math.sin(2 * math.pi * p - 0.5) * (1 if i == 0 else -1)
        pb.rotation_quaternion = local_rotation(pb, PITCH_AXIS, rel) @ local_rotation(pb, SNOUT, side)
        pb.keyframe_insert("rotation_quaternion", frame=f)

# ---- world, camera, light-free render (everything glows)
world = bpy.data.worlds.new("Black")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.0, 0.0, 0.0, 1.0)
scene.world = world
cam_data = bpy.data.cameras.new("Cam")
cam_data.lens = 70
cam = bpy.data.objects.new("Cam", cam_data)
scene.collection.objects.link(cam)
scene.camera = cam
look = centre + Vector((0.0, 0.0, -0.025))  # a little low: the snout swings down when the head comes up
cam.location = look + (Vector((-0.45, -1.0, 0.18)).normalized() * 0.9)
direction = look - cam.location
cam.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()

scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = 720
scene.render.resolution_y = 720
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'

# glow: a Glare node in the compositor (Blender 5: the compositor is a node group on the scene)
glow = "none"
try:
    tree = bpy.data.node_groups.new("Glow", "CompositorNodeTree")
    tree.interface.new_socket("Image", in_out='OUTPUT', socket_type='NodeSocketColor')
    rl = tree.nodes.new("CompositorNodeRLayers")
    glare = tree.nodes.new("CompositorNodeGlare")
    # Blender 5: the glare's type and quality are menu sockets
    for key, choices in (("Type", ("Bloom", "BLOOM")), ("Quality", ("High", "HIGH"))):
        for choice in choices:
            try:
                glare.inputs[key].default_value = choice
                break
            except Exception:
                pass
    for key, value in (("Threshold", 0.8), ("Strength", 0.45), ("Size", 0.45)):
        if key in glare.inputs:
            glare.inputs[key].default_value = value
    out = tree.nodes.new("NodeGroupOutput")
    tree.links.new(rl.outputs["Image"], glare.inputs["Image"])
    tree.links.new(glare.outputs["Image"], out.inputs[0])
    scene.compositing_node_group = tree
    glow = "compositor glare: " + str(glare.inputs["Type"].default_value)
except Exception as e:
    glow = "failed: " + str(e)

if HEADER:
    import bmesh as _bm
    me = src.data
    groups = {g.index: g.name for g in src.vertex_groups}
    order = {"head": 0, "ear.A": 1, "ear.B": 2}
    weights = []
    for v in me.vertices:
        w = [0.0, 0.0, 0.0]
        for g in v.groups:
            w[order[groups[g.group]]] = g.weight
        total = sum(w) or 1.0
        weights.append([x / total for x in w])
    b = _bm.new()
    b.from_mesh(me)
    b.edges.ensure_lookup_table()
    b.faces.ensure_lookup_table()
    edges = [(e.verts[0].index, e.verts[1].index, (e.link_faces[0].index if len(e.link_faces) > 0 else -1),
              (e.link_faces[1].index if len(e.link_faces) > 1 else -1)) for e in b.edges]
    faces = [[v.index for v in f.verts][:3] for f in b.faces]
    b.free()
    bpy.context.view_layer.update()  # the camera was placed from Python: bring its world matrix up to date
    view = cam.matrix_world.inverted()
    focal = cam_data.lens / cam_data.sensor_width
    N = 256
    head_table = [head_angle(i / N) for i in range(N)]
    ear_table = [ear_curve[int(i / N * STEPS) % STEPS] * 1.6 for i in range(N)]
    f3 = lambda v: "{ %.6ff, %.6ff, %.6ff }" % (v[0], v[1], v[2])
    lines = ["// Generated by Tools/beatdog/build_beat_dog.py from the husky model: do not edit by hand.",
             "// The BEAT DOG head for the live (real-time) version: rest positions in Blender's world space (Z up),",
             "// skin weights (head, ear A, ear B), edges with the faces either side (-1 = none), each face's first three",
             "// corners (for which way it faces), the rig's pivots and axes, the camera, and one beat of motion.",
             "#pragma once", "", "namespace beatdog", "{",
             "inline constexpr int kNumVerts = %d, kNumEdges = %d, kNumFaces = %d, kTableSize = %d;" % (len(me.vertices), len(edges), len(faces), N),
             "inline constexpr float kVerts[kNumVerts][3] = {"]
    lines += ["    " + f3(v.co) + "," for v in me.vertices]
    lines += ["};", "inline constexpr float kWeights[kNumVerts][3] = {"]
    lines += ["    { %.4ff, %.4ff, %.4ff }," % tuple(w) for w in weights]
    lines += ["};", "inline constexpr int kEdges[kNumEdges][4] = {"]
    lines += ["    { %d, %d, %d, %d }," % e for e in edges]
    lines += ["};", "inline constexpr int kFaces[kNumFaces][3] = {"]
    lines += ["    { %d, %d, %d }," % tuple(f) for f in faces]
    lines += ["};",
              "inline constexpr float kNeck[3] = " + f3(neck_root) + ";",
              "inline constexpr float kEarBase[2][3] = { " + f3((tipA.x, tipA.y, EAR_BASE_Z)) + ", " + f3((tipB.x, tipB.y, EAR_BASE_Z)) + " };",
              "inline constexpr float kPitchAxis[3] = " + f3(PITCH_AXIS) + ";",
              "inline constexpr float kSnout[3] = " + f3(SNOUT) + ";",
              "inline constexpr float kDown = %.6ff;      // the head's pitch at the bottom of the hit (radians)" % DOWN,
              "inline constexpr float kDip = 0.012f;       // how far the whole head drops at the bottom of the hit",
              "inline constexpr float kEarSide = %.6ff;  // the ears' outward wobble (radians)" % math.radians(5.0),
              "inline constexpr float kView[4][4] = {"]
    lines += ["    { %.6ff, %.6ff, %.6ff, %.6ff }," % tuple(view[r]) for r in range(4)]
    lines += ["};",
              "inline constexpr float kFocal = %.6ff; // lens / sensor width: image widths per unit of depth" % focal,
              "inline constexpr float kHeadAngle[kTableSize] = {"]
    lines += ["    " + ", ".join("%.5ff" % x for x in head_table[i:i + 8]) + "," for i in range(0, N, 8)]
    lines += ["};", "inline constexpr float kEarAngle[kTableSize] = {"]
    lines += ["    " + ", ".join("%.5ff" % x for x in ear_table[i:i + 8]) + "," for i in range(0, N, 8)]
    lines += ["};", "} // namespace beatdog", ""]
    with open(HEADER, "w", encoding="utf-8", newline=chr(10)) as fh:
        fh.write(chr(10).join(lines))
    print("INFO header %s: %d verts, %d edges, %d faces" % (HEADER, len(me.vertices), len(edges), len(faces)))

scene.render.resolution_x = scene.render.resolution_y = TILE
frames_dir = os.path.join(OUT_DIR, "beat_frames")
os.makedirs(frames_dir, exist_ok=True)
paths = []
for f in range(1, FRAMES + 1):
    scene.frame_set(f)
    scene.render.filepath = os.path.join(frames_dir, "f%02d.png" % f)
    bpy.ops.render.render(write_still=True)
    paths.append(scene.render.filepath)

# the sheet: frames left to right, top to bottom (Blender's image pixels start at the bottom row)
import numpy as np
rows = (FRAMES + COLS - 1) // COLS
sheet = np.zeros((rows * TILE, COLS * TILE, 4), dtype=np.float32)
for i, path in enumerate(paths):
    img = bpy.data.images.load(path)
    px = np.array(img.pixels[:], dtype=np.float32).reshape(TILE, TILE, 4)
    r, c = divmod(i, COLS)
    y0 = (rows - 1 - r) * TILE
    sheet[y0:y0 + TILE, c * TILE:(c + 1) * TILE] = px
    bpy.data.images.remove(img)
sheet[..., 3] = 1.0
out = bpy.data.images.new("BeatDogSheet", COLS * TILE, rows * TILE, alpha=False)
out.pixels = sheet.ravel()
out.filepath_raw = SHEET
out.file_format = 'JPEG'
scene.render.image_settings.file_format = 'JPEG'
scene.render.image_settings.color_mode = 'RGB'
scene.render.image_settings.quality = 88
out.save_render(SHEET, scene=scene)
bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUT_DIR, "beat_dog.blend"))
print("INFO " + json.dumps({"removed_pieces": removed_pieces, "removed_slivers": removed_slivers, "glow": glow, "sheet": SHEET,
                            "size": [COLS * TILE, rows * TILE]}))
