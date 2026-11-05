# ModelBaseColor.md の E-2：テスト用のモデル（同じ形を glTF と OBJ に書き出す）
# エンジンのリポジトリの一番上で：
#   & "C:\Program Files\Blender Foundation\Blender 5.2\blender.exe" --background --factory-startup --python Docs/Tasks/ModelBaseColorTest/make_test_model.py
import bpy, os

OUT = os.path.join(os.getcwd(), "project", "Sandbox", "Resources", "Model", "BaseColorTest")
os.makedirs(OUT, exist_ok=True)

# 空のシーンから
bpy.ops.wm.read_factory_settings(use_empty=True)

# 市松のテクスチャ（白と sRGB 0.5 の灰色。8 ピクセルずつ）
size = 64
img = bpy.data.images.new("checker", size, size, alpha=False)
px = []
for y in range(size):
    for x in range(size):
        v = 1.0 if ((x // 8) + (y // 8)) % 2 == 0 else 0.5
        px += [v, v, v, 1.0]
img.pixels = px
img.filepath_raw = os.path.join(OUT, "checker.png")
img.file_format = "PNG"
img.save()

# マテリアル（色はリニア。Blender の中の値が、そのまま glTF の baseColorFactor・OBJ の Kd に書かれる）
def make_material(name, color, use_texture):
    mat = bpy.data.materials.new(name)
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.5
    if use_texture:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = img
        if color == (1.0, 1.0, 1.0):
            nt.links.new(tex.outputs["Color"], bsdf.inputs["Base Color"])
        else:
            # 色 × テクスチャ（glTF の書き出しは、Multiply の Mix を baseColorFactor にする。OBJ は map_Kd を書かないので、mtl に手で足す）
            mix = nt.nodes.new("ShaderNodeMix")
            mix.data_type = "RGBA"
            mix.blend_type = "MULTIPLY"
            mix.inputs["Factor"].default_value = 1.0
            mix.inputs[7].default_value = (*color, 1.0)  # B（色）
            nt.links.new(tex.outputs["Color"], mix.inputs[6])  # A（テクスチャ）
            nt.links.new(mix.outputs[2], bsdf.inputs["Base Color"])
    return mat

# 部品（名前・色・テクスチャを使うか・Blender の X）
parts = [
    ("ColorDark",    (0.05, 0.20, 0.60), False, -3.0),
    ("ColorBright",  (1.00, 0.55, 0.10), False, -1.0),
    ("TextureOnly",  (1.00, 1.00, 1.00), True,   1.0),
    ("ColorTexture", (0.20, 0.80, 0.30), True,   3.0),
]
for name, color, use_tex, x in parts:
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=16, radius=0.8, location=(x, 0.0, 0.0))
    obj = bpy.context.active_object
    obj.name = name
    bpy.ops.object.shade_smooth()
    obj.data.materials.append(make_material(name, color, use_tex))

# 書き出し（glTF は .gltf＋.bin＋png、OBJ は .obj＋.mtl）
bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, "BaseColorTest.gltf"), export_format="GLTF_SEPARATE", export_animations=False)
bpy.ops.wm.obj_export(filepath=os.path.join(OUT, "BaseColorTest.obj"), export_materials=True, path_mode="RELATIVE", export_uv=True, export_normals=True)
print("DONE", os.listdir(OUT))
