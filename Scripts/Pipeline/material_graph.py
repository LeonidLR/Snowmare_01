"""Unreal material graph (as dumped by unreal_export_inventory.py) -> Blender shader nodes, and texture role tables.

Every rebuilt material exposes its channels on labelled Reroute nodes so later stages can rewire them:
    OUT_base_color   colour without the view-dependent Fresnel term (what gets baked)
    OUT_sheen        colour that Unreal adds * Fresnel (0 where the material has no Fresnel term)
    OUT_roughness / OUT_metallic / OUT_ao   floats exactly as Unreal feeds them (sRGB decode + Power applied)
    OUT_emissive     colour
    OUT_normal       world-space normal (Normal Map node in the tangent space of the ORIGINAL uv)
Normal maps are DirectX (green down) in Unreal; the green channel is flipped here for Blender (OpenGL).
"""
import os
import re

PROPS = ("base_color", "roughness", "metallic", "ambient_occlusion", "emissive_color", "normal", "opacity",
         "opacity_mask", "specular")
DEFAULTS = {"base_color": (0.0, 0.0, 0.0), "roughness": 0.5, "metallic": 0.0, "ambient_occlusion": 1.0,
            "emissive_color": (0.0, 0.0, 0.0), "specular": 0.5, "opacity": 1.0, "opacity_mask": 1.0}
CHANNELS = {"R": "Red", "G": "Green", "B": "Blue"}


def sampler_is_normal(node):
    return "NORMAL" in str(node.get("sampler_type", "")).upper()


def short_name(asset_path):
    return asset_path.split(".")[-1] if asset_path else None


# ----------------------------------------------------------------------------------------------- texture roles

def texture_roles(inventory):
    """Ground-truth role table: texture -> role, derived from the material graph; the name guess as cross-check."""
    from pipeline_common import NAME_ROLE_PATTERNS
    usage = {}

    def visit(node, prop, mat, chain):
        if node.get("texture"):
            u = usage.setdefault(node["texture"], {"feeds": [], "materials": set()})
            u["feeds"].append({"material": short_name(mat), "property": prop, "channel": node.get("output") or "RGB",
                               "sampler_type": str(node.get("sampler_type", "")).split(".")[-1].split(":")[0],
                               "via": chain})
            u["materials"].add(short_name(mat))
        for c in node.get("inputs", []):
            visit(c, prop, mat, chain + [c["node"]])

    for mpath, m in inventory["materials"].items():
        for prop, node in m.get("graph", {}).items():
            visit(node, prop, mpath, [node["node"]])

    table = {}
    for tpath, tinfo in inventory["textures"].items():
        name = short_name(tpath)
        guess = next((role for pat, role in NAME_ROLE_PATTERNS if re.search(pat, name, re.I)), "unknown")
        u = usage.get(tpath)
        warnings = []
        if u:
            props = sorted({f["property"] for f in u["feeds"]})
            channels = {f["property"]: f["channel"] for f in u["feeds"]}
            if props == ["normal"]:
                role = "normal"
            elif props == ["base_color"]:
                role = "base_color"
            elif props == ["emissive_color"]:
                role = "emissive"
            elif set(props) <= {"ambient_occlusion", "roughness", "metallic"} and len(props) > 1:
                role = "orm_packed"
            elif len(props) == 1:
                role = {"ambient_occlusion": "ambient_occlusion"}.get(props[0], props[0])
            else:
                role = "mixed:" + "+".join(props)
            method = "material_graph"
            for f in u["feeds"]:
                if f["property"] != "normal" and "NORMAL" in f["sampler_type"].upper():
                    warnings.append("scalar/colour data sampled with a Normal sampler in %s (%s): Unreal reconstructs "
                                    "Z / remaps to -1..1 - authoring error, baked as raw linear channel (intent)"
                                    % (f["material"], f["property"]))
            if tinfo.get("srgb") and role in ("orm_packed", "roughness", "metallic", "ambient_occlusion", "normal"):
                warnings.append("sRGB is ON for a data texture: Unreal decodes it with the sRGB curve before use; "
                                "the bake reproduces the decoded values")
        else:
            role, channels, method = guess, {}, "name_suffix"
            warnings.append("not reached from any material output in the graph")
        table[name] = {
            "asset": tpath, "role": role, "channels": channels, "method": method, "name_guess": guess,
            "name_guess_agrees": (guess == role), "srgb": tinfo.get("srgb"), "compression": tinfo.get("compression"),
            "size": [tinfo.get("size_x"), tinfo.get("size_y")],
            "materials": sorted(u["materials"]) if u else [], "warnings": warnings,
        }
    return table


# ----------------------------------------------------------------------------------------------- node building

class GraphBuilder:
    """Builds one Blender material from one Unreal material description."""

    def __init__(self, mat, minfo, inventory, tex_dir, uv_name, warnings):
        self.mat = mat
        self.minfo = minfo
        self.inventory = inventory
        self.tex_dir = tex_dir
        self.uv_name = uv_name
        self.warnings = warnings
        self.nt = mat.node_tree
        self.images = {}
        self.x = -1600
        self.y = 0
        self.uv_node = None

    def node(self, kind, label=None):
        n = self.nt.nodes.new(kind)
        n.location = (self.x, self.y)
        self.y -= 160
        if label:
            n.label = label
        return n

    def uv(self):
        if self.uv_node is None:
            self.uv_node = self.node("ShaderNodeUVMap", "source uv")
            self.uv_node.uv_map = self.uv_name
        return self.uv_node

    def image_node(self, tex_path, as_normal):
        key = (tex_path, as_normal)
        if key in self.images:
            return self.images[key]
        import bpy
        name = short_name(tex_path)
        file = os.path.join(self.tex_dir, name + ".png")
        img = bpy.data.images.get(name) or bpy.data.images.load(file, check_existing=True)
        img.name = name
        tinfo = self.inventory["textures"].get(tex_path, {})
        img.colorspace_settings.name = "sRGB" if (tinfo.get("srgb") and not as_normal) else "Non-Color"
        n = self.node("ShaderNodeTexImage", name)
        n.image = img
        n.interpolation = "Linear"
        self.nt.links.new(self.uv().outputs["UV"], n.inputs["Vector"])
        self.images[key] = n
        return n

    def math(self, op, a, b=None):
        n = self.node("ShaderNodeMath")
        n.operation = op
        self.feed(n.inputs[0], a)
        if b is not None:
            self.feed(n.inputs[1], b)
        return n.outputs[0]

    def vmath(self, op, a, b=None):
        n = self.node("ShaderNodeVectorMath")
        n.operation = op
        self.feed(n.inputs[0], a, vector=True)
        if b is not None:
            self.feed(n.inputs[1], b, vector=True)
        return n.outputs[0]

    def feed(self, socket, value, vector=False):
        """Connect a socket or set a constant (float or 3-tuple)."""
        if hasattr(value, "is_output"):
            self.nt.links.new(value, socket)
        elif isinstance(value, (tuple, list)):
            v = list(value)[:3]
            socket.default_value = v if len(socket.default_value) == 3 else v + [1.0]
        else:
            try:
                socket.default_value = value
            except (TypeError, ValueError):
                socket.default_value = [value] * len(socket.default_value)

    @staticmethod
    def is_color(v):
        return isinstance(v, (tuple, list)) or (hasattr(v, "type") and v.type in ("RGBA", "VECTOR"))

    def to_color(self, v):
        if self.is_color(v):
            return v
        if hasattr(v, "is_output"):
            c = self.node("ShaderNodeCombineColor")
            for i in range(3):
                self.nt.links.new(v, c.inputs[i])
            return c.outputs[0]
        return (v, v, v)

    def binary(self, op, a, b):
        if self.is_color(a) or self.is_color(b):
            vop = {"MULTIPLY": "MULTIPLY", "ADD": "ADD", "SUBTRACT": "SUBTRACT", "POWER": "POWER"}[op]
            if vop == "POWER":  # per-channel power: approximate with the scalar exponent
                vop = "MULTIPLY"
                self.warnings.append("%s: Power on a colour approximated" % self.mat.name)
            return self.vmath(vop, self.to_color(a), self.to_color(b))
        if not hasattr(a, "is_output") and not hasattr(b, "is_output"):
            return {"MULTIPLY": a * b, "ADD": a + b, "SUBTRACT": a - b, "POWER": (max(a, 0.0) ** b)}[op]
        return self.math(op, a, b)

    def eval(self, node, prop, fresnel):
        """Returns an output socket or a constant for an Unreal expression description."""
        kind = node["node"]
        inputs = {c.get("input", ""): c for c in node.get("inputs", [])}

        def arg(name, const_key, default):
            if name in inputs:
                return self.eval(inputs[name], prop, fresnel)
            v = node.get(const_key)
            return default if v is None else v

        if kind in ("TextureSample", "TextureSampleParameter2D", "TextureObjectParameter"):
            tex = node.get("texture")
            if not tex:
                self.warnings.append("%s/%s: texture sample without texture" % (self.mat.name, prop))
                return DEFAULTS.get(prop, 0.5)
            as_normal = sampler_is_normal(node) and prop == "normal"
            img = self.image_node(tex, as_normal)
            out = (node.get("output") or "RGB").upper()
            if out in ("RGB", "RGBA", ""):
                return img.outputs["Color"]
            if out == "A":
                return img.outputs["Alpha"]
            sep = self.node("ShaderNodeSeparateColor")
            self.nt.links.new(img.outputs["Color"], sep.inputs[0])
            return sep.outputs[CHANNELS.get(out, "Red")]
        if kind == "Constant":
            return float(node.get("r", 0.0))
        if kind in ("Constant3Vector", "Constant4Vector", "VectorParameter"):
            c = node.get("constant") or node.get("default_value") or [0, 0, 0]
            return tuple(c[:3])
        if kind == "ScalarParameter":
            return float(node.get("default_value", 0.0))
        if kind == "Fresnel":
            return fresnel
        if kind in ("Multiply", "Add", "Subtract"):
            a = arg("A", "const_a", 0.0 if kind != "Multiply" else 1.0)
            b = arg("B", "const_b", 1.0)
            return self.binary(kind.upper(), a, b)
        if kind == "Power":
            base = arg("Base", "_", 0.0)
            exp = arg("Exp", "const_exponent", 2.0)
            if self.is_color(base):
                return self.binary("POWER", base, exp)
            return self.binary("POWER", base, exp)
        if kind == "OneMinus":
            v = arg("Input", "_", 0.0)
            return self.binary("SUBTRACT", 1.0 if not self.is_color(v) else (1.0, 1.0, 1.0), v)
        if kind == "LinearInterpolate":
            a, b, alpha = arg("A", "const_a", 0.0), arg("B", "const_b", 1.0), arg("Alpha", "const_alpha", 0.5)
            mix = self.node("ShaderNodeMix")
            mix.data_type = "RGBA" if (self.is_color(a) or self.is_color(b)) else "FLOAT"
            idx = {"FLOAT": (0, 2, 3), "RGBA": (0, 6, 7)}[mix.data_type]
            self.feed(mix.inputs[idx[0]], alpha)
            self.feed(mix.inputs[idx[1]], self.to_color(a) if mix.data_type == "RGBA" else a)
            self.feed(mix.inputs[idx[2]], self.to_color(b) if mix.data_type == "RGBA" else b)
            return mix.outputs[0 if mix.data_type == "FLOAT" else 2]
        self.warnings.append("%s/%s: unsupported expression %s - default used" % (self.mat.name, prop, kind))
        return DEFAULTS.get(prop, 0.5)

    def reroute(self, label, value, vector=False):
        r = self.nt.nodes.new("NodeReroute")
        r.label = label
        r.name = label
        r.location = (-200, self.y)
        self.y -= 60
        src = value
        if not hasattr(value, "is_output"):
            if isinstance(value, (tuple, list)):
                n = self.node("ShaderNodeRGB", label + "_const")
                n.outputs[0].default_value = list(value)[:3] + [1.0]
            else:
                n = self.node("ShaderNodeValue", label + "_const")
                n.outputs[0].default_value = float(value)
            src = n.outputs[0]
        self.nt.links.new(src, r.inputs[0])
        return r.outputs[0]

    def build(self, render_fresnel=True):
        graph = self.minfo.get("graph", {})
        out = {}
        for prop in ("base_color", "roughness", "metallic", "ambient_occlusion", "emissive_color"):
            self.y = {"base_color": 1200, "roughness": 600, "metallic": 200, "ambient_occlusion": -200,
                      "emissive_color": -600}[prop]
            if prop in graph:
                out[prop] = self.eval(graph[prop], prop, 0.0)
            else:
                out[prop] = DEFAULTS[prop]
        # Unreal adds Fresnel * k to some base colours: k = value(F=1) - value(F=0) (the graphs are linear in F).
        sheen = (0.0, 0.0, 0.0)
        if "base_color" in graph and self._has(graph["base_color"], "Fresnel"):
            self.y = 1600
            bc1 = self.eval(graph["base_color"], "base_color", 1.0)
            sheen = self.vmath("SUBTRACT", self.to_color(bc1), self.to_color(out["base_color"]))
        self.y = -1000
        normal = None
        if "normal" in graph:
            n = graph["normal"]
            tex_node = n if n.get("texture") else None
            if tex_node is None or n["node"] not in ("TextureSample", "TextureSampleParameter2D"):
                self.warnings.append("%s: normal input is not a plain texture sample (%s) - mesh normal used"
                                     % (self.mat.name, n["node"]))
            else:
                img = self.image_node(tex_node["texture"], True)
                sep = self.node("ShaderNodeSeparateColor")
                self.nt.links.new(img.outputs["Color"], sep.inputs[0])
                inv = self.math("SUBTRACT", 1.0, sep.outputs["Green"])  # DirectX -> OpenGL
                comb = self.node("ShaderNodeCombineColor")
                self.nt.links.new(sep.outputs["Red"], comb.inputs[0])
                self.nt.links.new(inv, comb.inputs[1])
                self.nt.links.new(sep.outputs["Blue"], comb.inputs[2])
                nm = self.node("ShaderNodeNormalMap")
                nm.space = "TANGENT"
                nm.uv_map = self.uv_name
                self.nt.links.new(comb.outputs[0], nm.inputs["Color"])
                normal = nm.outputs["Normal"]
        if normal is None:
            geo = self.node("ShaderNodeNewGeometry")
            normal = geo.outputs["Normal"]

        sockets = {
            "base_color": self.reroute("OUT_base_color", self.to_color(out["base_color"])),
            "sheen": self.reroute("OUT_sheen", sheen),
            "roughness": self.reroute("OUT_roughness", out["roughness"]),
            "metallic": self.reroute("OUT_metallic", out["metallic"]),
            "ao": self.reroute("OUT_ao", out["ambient_occlusion"]),
            "emissive": self.reroute("OUT_emissive", self.to_color(out["emissive_color"])),
            "normal": self.reroute("OUT_normal", normal),
        }
        connect_principled(self.mat, sockets, render_fresnel)
        return sockets

    @staticmethod
    def _has(node, kind):
        if node["node"] == kind:
            return True
        return any(GraphBuilder._has(c, kind) for c in node.get("inputs", []))


def connect_principled(mat, sockets=None, with_fresnel=True):
    """Principled BSDF from the OUT_* reroutes: base + sheen * Fresnel(UE: 0.04 + 0.96 (1-NdotV)^5)."""
    nt = mat.node_tree
    if sockets is None:
        sockets = {k: nt.nodes["OUT_" + k].outputs[0] for k in
                   ("base_color", "sheen", "roughness", "metallic", "ao", "emissive", "normal") if ("OUT_" + k) in nt.nodes}
    for n in [n for n in nt.nodes if n.type in ("BSDF_PRINCIPLED", "OUTPUT_MATERIAL", "EMISSION")
              or n.label.startswith("FRESNEL_")]:
        nt.nodes.remove(n)
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.location = (200, 0)
    outn = nt.nodes.new("ShaderNodeOutputMaterial")
    outn.location = (600, 0)
    nt.links.new(bsdf.outputs[0], outn.inputs["Surface"])
    base = sockets["base_color"]
    if with_fresnel and "sheen" in sockets:
        geo = nt.nodes.new("ShaderNodeNewGeometry")
        geo.label = "FRESNEL_geo"
        dot = nt.nodes.new("ShaderNodeVectorMath")
        dot.operation = "DOT_PRODUCT"
        dot.label = "FRESNEL_dot"
        nt.links.new(geo.outputs["Normal"], dot.inputs[0])
        nt.links.new(geo.outputs["Incoming"], dot.inputs[1])
        chain = [("ABSOLUTE", None), ("SUBTRACT_FROM_1", None), ("POWER", 5.0), ("MULTIPLY_ADD", (0.96, 0.04))]
        prev = dot.outputs["Value"]
        for op, val in chain:
            m = nt.nodes.new("ShaderNodeMath")
            m.label = "FRESNEL_" + op
            if op == "SUBTRACT_FROM_1":
                m.operation = "SUBTRACT"
                m.inputs[0].default_value = 1.0
                nt.links.new(prev, m.inputs[1])
            elif op == "MULTIPLY_ADD":
                m.operation = "MULTIPLY_ADD"
                nt.links.new(prev, m.inputs[0])
                m.inputs[1].default_value, m.inputs[2].default_value = val
            else:
                m.operation = op
                nt.links.new(prev, m.inputs[0])
                if val is not None:
                    m.inputs[1].default_value = val
            prev = m.outputs[0]
        mul = nt.nodes.new("ShaderNodeVectorMath")
        mul.operation = "SCALE"
        mul.label = "FRESNEL_scale"
        nt.links.new(sockets["sheen"], mul.inputs[0])
        nt.links.new(prev, mul.inputs["Scale"])
        add = nt.nodes.new("ShaderNodeVectorMath")
        add.operation = "ADD"
        add.label = "FRESNEL_add"
        nt.links.new(base, add.inputs[0])
        nt.links.new(mul.outputs[0], add.inputs[1])
        base = add.outputs[0]
    nt.links.new(base, bsdf.inputs["Base Color"])
    nt.links.new(sockets["roughness"], bsdf.inputs["Roughness"])
    nt.links.new(sockets["metallic"], bsdf.inputs["Metallic"])
    nt.links.new(sockets["normal"], bsdf.inputs["Normal"])
    if "emissive" in sockets:
        nt.links.new(sockets["emissive"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = 1.0
    return bsdf


def build_material(name, minfo, inventory, tex_dir, uv_name, warnings):
    import bpy
    mat = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.node_tree.nodes.clear()
    GraphBuilder(mat, minfo, inventory, tex_dir, uv_name, warnings).build()
    return mat
