"""Builds the Teleop material assets from code, so they can be regenerated and reviewed in git.

Run in the editor (Python Editor Script Plugin):
    import importlib, teleop_materials; importlib.reload(teleop_materials); teleop_materials.build_all()

Shader logic lives in <Project>/Shaders (virtual path /Reprojection, mapped by the ReprojectionShaders
module); these materials are thin wrappers around Custom nodes that #include it.
"""

import unreal

FOLDER = "/Game/Teleop/Materials"
EYE_TEST_USH = "/Reprojection/Teleop/EyeTest.ush"

MEL = unreal.MaterialEditingLibrary
EAL = unreal.EditorAssetLibrary

RED = unreal.LinearColor(1.0, 0.0, 0.0, 1.0)
GREEN = unreal.LinearColor(0.0, 1.0, 0.0, 1.0)


def _new_material(name):
    """Create (or recreate from scratch) a material asset."""
    # Editor asset functions refuse to run during Play-In-Editor (does_asset_exist returns False), which
    # would make create_asset pop a blocking "Overwrite Existing Object" dialog.
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play-In-Editor before building materials.")
    path = f"{FOLDER}/{name}"
    if EAL.does_asset_exist(path):
        mat = EAL.load_asset(path)
        MEL.delete_all_material_expressions(mat)
        return mat
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, FOLDER, unreal.Material, unreal.MaterialFactoryNew())


def _expr(mat, cls, x, y):
    return MEL.create_material_expression(mat, cls, x, y)


def _connect(src, src_out, dst, dst_in_candidates):
    """Connect src -> dst, trying the given input names (pin names differ between engine versions)."""
    for dst_in in dst_in_candidates:
        if MEL.connect_material_expressions(src, src_out, dst, dst_in):
            return dst_in
    raise RuntimeError(f"Could not connect {src.get_name()} -> {dst.get_name()} via {dst_in_candidates}")


def _eye_index_node(mat, x, y):
    custom = _expr(mat, unreal.MaterialExpressionCustom, x, y)
    custom.set_editor_property("description", "EyeIndex")
    custom.set_editor_property("code", "return Teleop_EyeIndex();")
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("inputs", [])
    custom.set_editor_property("include_file_paths", [EYE_TEST_USH])
    return custom


def _red_green_lerp(mat, alpha, x, y):
    red = _expr(mat, unreal.MaterialExpressionConstant3Vector, x - 250, y - 120)
    red.set_editor_property("constant", RED)
    green = _expr(mat, unreal.MaterialExpressionConstant3Vector, x - 250, y + 20)
    green.set_editor_property("constant", GREEN)
    lerp = _expr(mat, unreal.MaterialExpressionLinearInterpolate, x, y)
    _connect(red, "", lerp, ["A"])
    _connect(green, "", lerp, ["B"])
    _connect(alpha, "", lerp, ["Alpha"])
    return lerp


def _finish(mat):
    MEL.recompile_material(mat)
    EAL.save_loaded_asset(mat)
    unreal.log(f"[teleop_materials] built {mat.get_path_name()}")


def build_unlit_color():
    """M_TeleopUnlitColor: unlit, emissive = vector parameter 'Color'. Used by the operator room."""
    mat = _new_material("M_TeleopUnlitColor")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.015, 0.015, 0.02, 1.0))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_lit_color():
    """M_TeleopLitColor: default lit, base color = vector parameter 'Color'. Used by the proxy hands."""
    mat = _new_material("M_TeleopLitColor")
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -300, 0)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.5, 0.5, 0.5, 1.0))
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -300, 200)
    rough.set_editor_property("r", 0.45)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    _finish(mat)


def build_ghost():
    """M_TeleopGhost: translucent unlit with a Fresnel rim; 'Color' (vector), 'Opacity' (scalar). The observer view's
    ghosts (operator head and hands, undelayed); used on static and skeletal meshes."""
    mat = _new_material("M_TeleopGhost")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("used_with_skeletal_mesh", True)
    color = _expr(mat, unreal.MaterialExpressionVectorParameter, -700, -100)
    color.set_editor_property("parameter_name", "Color")
    color.set_editor_property("default_value", unreal.LinearColor(0.1, 1.0, 0.3, 1.0))
    opacity = _expr(mat, unreal.MaterialExpressionScalarParameter, -700, 200)
    opacity.set_editor_property("parameter_name", "Opacity")
    opacity.set_editor_property("default_value", 0.35)
    fresnel = _expr(mat, unreal.MaterialExpressionFresnel, -700, 50)
    rim = _expr(mat, unreal.MaterialExpressionAdd, -450, 0)
    rim.set_editor_property("const_b", 0.5)
    _connect(fresnel, "", rim, ["A"])
    emissive = _expr(mat, unreal.MaterialExpressionMultiply, -250, -50)
    _connect(color, "", emissive, ["A"])
    _connect(rim, "", emissive, ["B"])
    MEL.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rim_alpha = _expr(mat, unreal.MaterialExpressionAdd, -450, 150)
    rim_alpha.set_editor_property("const_b", 0.6)
    _connect(fresnel, "", rim_alpha, ["A"])
    alpha = _expr(mat, unreal.MaterialExpressionMultiply, -250, 200)
    _connect(opacity, "", alpha, ["A"])
    _connect(rim_alpha, "", alpha, ["B"])
    MEL.connect_material_property(alpha, "", unreal.MaterialProperty.MP_OPACITY)
    _finish(mat)


def build_text_unlit():
    """M_TeleopTextUnlit: the engine's DefaultTextMaterialOpaque (vertex colour, masked by the font) made unlit, so the
    observer view's text keeps its colour whatever the scene lighting (the engine one is default lit)."""
    if unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is not None:
        raise RuntimeError("Stop Play-In-Editor before building materials.")
    path = f"{FOLDER}/M_TeleopTextUnlit"
    if EAL.does_asset_exist(path):
        EAL.delete_asset(path)
    mat = EAL.duplicate_asset("/Engine/EngineMaterials/DefaultTextMaterialOpaque", path)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    vertex_color = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_BASE_COLOR)
    MEL.connect_material_property(vertex_color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_eye_test_pp():
    """M_EyeTest_PP: post process after tonemapping, left eye red / right eye green (pixel stage)."""
    mat = _new_material("M_EyeTest_PP")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    eye = _eye_index_node(mat, -600, 0)
    lerp = _red_green_lerp(mat, eye, -250, 0)
    MEL.connect_material_property(lerp, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_eye_test_vertex():
    """M_EyeTest_Vertex: unlit surface; eye index read and color chosen in the VERTEX shader,
    passed to the pixel shader through a Vertex Interpolator (checks the eye index for WPO, HANDOFF §7.1)."""
    mat = _new_material("M_EyeTest_Vertex")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    eye = _eye_index_node(mat, -900, 0)
    lerp = _red_green_lerp(mat, eye, -550, 0)
    interp = _expr(mat, unreal.MaterialExpressionVertexInterpolator, -250, 0)
    used_in = _connect(lerp, "", interp, ["VS", "Input", ""])
    for out_name in ("PS", ""):
        if MEL.connect_material_property(interp, out_name, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
            break
    else:
        raise RuntimeError("Could not connect VertexInterpolator output to Emissive")
    unreal.log(f"[teleop_materials] vertex interpolator input pin: '{used_in}', output pin: '{out_name}'")
    _finish(mat)


PRESENT_USH = "/Reprojection/Teleop/Present.ush"
DEPTH_MESH_USH = "/Reprojection/Teleop/DepthMesh.ush"
DEPTH_DEGRADE_USH = "/Reprojection/Teleop/DepthDegrade.ush"
HUD_STENCIL = 1   # ATeleopOperatorPawn::HudStencilValue
MESH_STENCIL = 2  # ATeleopPresenter::DepthMeshStencilValue

CAM_INPUTS = ["CamPosL", "CamFwdL", "CamRightL", "CamUpL", "CamPosR", "CamFwdR", "CamRightR", "CamUpR"]
MAP_INPUTS = ["MapX", "MapY", "MapZ", "MapT"]

PRESENT_CODE = f"""
// HUD (stencil {HUD_STENCIL}) and, in M4, the M3 depth mesh (stencil {MESH_STENCIL}) stay visible.
if (abs(Stencil.r - {HUD_STENCIL}.0) < 0.5 || abs(Stencil.r - {MESH_STENCIL}.0) < 0.5)
{{
    return SceneColor.rgb;
}}
float2 ViewportUV = SvPositionToViewportUV(Parameters.SvPosition);
float3 C;
if (Mode < 0.5)
{{
    C = Teleop_PresentM0(ViewportUV, LeftTex, LeftTexSampler, RightTex, RightTexSampler, TanHalfFov.xy, Feather);
}}
else
{{
    FTeleopEyeCam CL; CL.Pos = CamPosL; CL.Fwd = CamFwdL; CL.Right = CamRightL; CL.Up = CamUpL;
    FTeleopEyeCam CR; CR.Pos = CamPosR; CR.Fwd = CamFwdR; CR.Right = CamRightR; CR.Up = CamUpR;
    C = Teleop_PresentSphere(ViewportUV, LeftTex, LeftTexSampler, RightTex, RightTexSampler, CL, CR, TanHalfFov.xy,
        SphereR, Feather, RoomSeat, MapX, MapY, MapZ, MapT);
}}
// Frames hold the capture's scene color, which is already pre-exposed like this view's scene color
// (both use the project's fixed exposure; measured M0 == GT brightness). ExposureScale is a manual trim.
return C * ExposureScale;
"""

MESH_WPO_CODE = """
float Valid;
float3 Offset = Teleop_DepthMeshOffset(UV.xy, LocalPos, GridStep.xy, DepthL, DepthLSampler, DepthR, DepthRSampler,
    CamPosL, CamFwdL, CamRightL, CamUpL, CamPosR, CamFwdR, CamRightR, CamUpR, TanHalfFov.xy, EdgeDispPx, DispScale,
    MapX, MapY, MapZ, MapT, Valid);
return float4(Offset, Valid);
"""

MESH_COLOR_CODE = "return Teleop_SampleEyeFrame(LeftTex, LeftTexSampler, RightTex, RightTexSampler, UV.xy).rgb * ExposureScale;"

MINMAX_CODE = "return float3(Teleop_DepthBlockMinMax(Src, SrcSampler, UV.xy, SrcTexel.xy, OutTexel.xy, K), 0);"

DEGRADE_CODE = """
return Teleop_DepthDegrade(MinMax, MinMaxSampler, UV.xy, Texel.xy, FocalPx, BaselineCm, SigmaDisp, Quantize,
    ZMin, ZMax, PHole, HoleCellPx, EdgeHolePx, EdgeDispPx, FattenPx, Seed).xxx;
"""

COPY_CODE = "return Texture2DSampleLevel(Src, SrcSampler, UV.xy, 0).rrr;"

CHECKER_CODE = """
float2 Cell = floor(UV.xy * N);
return (fmod(Cell.x + Cell.y, 2.0) < 0.5) ? float3(0.85, 0.85, 0.85) : float3(0.02, 0.02, 0.02);
"""


class _Graph:
    """Small helper: named parameter nodes + Custom nodes wired by input name."""

    def __init__(self, mat):
        self.mat = mat
        self.nodes = {}
        self.y = -1200

    def _next(self):
        self.y += 110
        return self.y

    def scalar(self, name, default=0.0):
        n = _expr(self.mat, unreal.MaterialExpressionScalarParameter, -1400, self._next())
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", default)
        self.nodes[name] = (n, "")
        return n

    def vector(self, name, default=(0.0, 0.0, 0.0)):
        n = _expr(self.mat, unreal.MaterialExpressionVectorParameter, -1400, self._next())
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("default_value", unreal.LinearColor(default[0], default[1], default[2], 0.0))
        self.nodes[name] = (n, "")
        return n

    def texture(self, name, as_input=None):
        n = _texture_object_param(self.mat, name, -1400, self._next())
        self.nodes[as_input or name] = (n, "")
        return n

    def node(self, key, expr, out=""):
        self.nodes[key] = (expr, out)
        return expr

    def custom(self, code, out_type, include, inputs, x=-500, y=0, desc="Teleop"):
        c = _expr(self.mat, unreal.MaterialExpressionCustom, x, y)
        c.set_editor_property("description", desc)
        c.set_editor_property("code", code)
        c.set_editor_property("output_type", out_type)
        c.set_editor_property("include_file_paths", [include] if include else [])
        ins = []
        for n in inputs:
            ci = unreal.CustomInput()
            ci.set_editor_property("input_name", n)
            ins.append(ci)
        c.set_editor_property("inputs", ins)
        for n in inputs:
            src, out = self.nodes[n]
            _connect(src, out, c, [n])
        return c


def _texture_object_param(mat, name, x, y):
    p = _expr(mat, unreal.MaterialExpressionTextureObjectParameter, x, y)
    p.set_editor_property("parameter_name", name)
    p.set_editor_property("texture", unreal.load_asset("/Engine/EngineResources/Black"))
    p.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    return p


def _scene_texture(mat, tex_id, x, y):
    s = _expr(mat, unreal.MaterialExpressionSceneTexture, x, y)
    s.set_editor_property("scene_texture_id", tex_id)
    return s


def _frame_params(g):
    g.texture("LeftFrame", "LeftTex")
    g.texture("RightFrame", "RightTex")
    g.vector("TanHalfFov", (1.428, 1.071, 0.0))  # 110 deg, 4:3
    for n in CAM_INPUTS + MAP_INPUTS:
        g.vector(n)
    g.scalar("ExposureScale", 1.0)


def build_present():
    """M_TeleopPresent: post process (Scene Color Before Bloom). Mode 0 = M0 head-locked, 1 = sphere (M1/M2/M4)."""
    mat = _new_material("M_TeleopPresent")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)
    g = _Graph(mat)
    _frame_params(g)
    g.scalar("Feather", 0.01)
    g.scalar("Mode", 0.0)
    g.scalar("SphereR", 100.0)
    g.vector("RoomSeat")
    g.node("SceneColor", _scene_texture(mat, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, -1400, 1400), "Color")
    g.node("Stencil", _scene_texture(mat, unreal.SceneTextureId.PPI_CUSTOM_STENCIL, -1400, 1550), "Color")
    inputs = ["LeftTex", "RightTex", "TanHalfFov", "Feather", "ExposureScale", "SceneColor", "Stencil", "Mode", "SphereR",
              "RoomSeat"] + CAM_INPUTS + MAP_INPUTS
    custom = g.custom(PRESENT_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, PRESENT_USH, inputs, desc="TeleopPresent")
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_depth_mesh():
    """M_TeleopDepthMesh (M3): unlit, masked, two-sided. WPO displaces the grid with the degraded depth in the vertex
    shader; triangles with invalid depth or across discontinuities are masked out; color = the eye's own frame."""
    mat = _new_material("M_TeleopDepthMesh")
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    g = _Graph(mat)
    _frame_params(g)
    g.texture("LeftDepth", "DepthL")
    g.texture("RightDepth", "DepthR")
    g.vector("GridStep", (1.0 / 640.0, 1.0 / 480.0, 0.0))
    g.scalar("EdgeDispPx", 3.0)
    g.scalar("DispScale", 2867.0)  # reference f * b (px * cm): 1280 px / 110 deg, 64 mm
    g.node("UV", _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 1400))
    local = _expr(mat, unreal.MaterialExpressionLocalPosition, -1400, 1500)
    local.set_editor_property("included_offsets", unreal.PositionIncludedOffsets.EXCLUDE_OFFSETS)
    g.node("LocalPos", local)

    wpo_inputs = ["UV", "LocalPos", "GridStep", "DepthL", "DepthR"] + CAM_INPUTS + ["TanHalfFov", "EdgeDispPx", "DispScale"] + MAP_INPUTS
    wpo = g.custom(MESH_WPO_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT4, DEPTH_MESH_USH, wpo_inputs, x=-700, y=-300, desc="DepthMeshWPO")
    offset = _expr(mat, unreal.MaterialExpressionComponentMask, -400, -300)
    for c, v in (("r", True), ("g", True), ("b", True), ("a", False)):
        offset.set_editor_property(c, v)
    _connect(wpo, "", offset, ["", "Input"])
    MEL.connect_material_property(offset, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)

    valid = _expr(mat, unreal.MaterialExpressionComponentMask, -400, -150)
    for c, v in (("r", False), ("g", False), ("b", False), ("a", True)):
        valid.set_editor_property(c, v)
    _connect(wpo, "", valid, ["", "Input"])
    interp = _expr(mat, unreal.MaterialExpressionVertexInterpolator, -250, -150)
    _connect(valid, "", interp, ["VS"])
    g.node("Valid", interp, "PS")
    mask = g.custom("return Valid > 0.999 ? 1.0 : 0.0;", unreal.CustomMaterialOutputType.CMOT_FLOAT1, None, ["Valid"], x=-100, y=-150, desc="ValidMask")
    MEL.connect_material_property(mask, "", unreal.MaterialProperty.MP_OPACITY_MASK)

    color = g.custom(MESH_COLOR_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, PRESENT_USH, ["LeftTex", "RightTex", "UV", "ExposureScale"],
                     x=-400, y=100, desc="DepthMeshColor")
    MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def _render_target_material(name):
    mat = _new_material(name)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    g = _Graph(mat)
    g.node("UV", _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 1400))
    return mat, g


def build_depth_passes():
    """Depth degradation passes (HANDOFF §5), drawn with DrawMaterialToRenderTarget."""
    mat, g = _render_target_material("M_TeleopDepthMinMax")
    g.texture("Src")
    g.vector("SrcTexel", (1.0 / 1280.0, 1.0 / 960.0, 0.0))
    g.vector("OutTexel", (1.0 / 640.0, 1.0 / 480.0, 0.0))
    g.scalar("K", 2.0)
    c = g.custom(MINMAX_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, DEPTH_DEGRADE_USH, ["Src", "UV", "SrcTexel", "OutTexel", "K"], desc="DepthMinMax")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)

    mat, g = _render_target_material("M_TeleopDepthDegrade")
    g.texture("MinMax")
    g.vector("Texel", (1.0 / 640.0, 1.0 / 480.0, 0.0))
    for n, d in (("FocalPx", 224.0), ("BaselineCm", 6.4), ("SigmaDisp", 0.2), ("Quantize", 1.0), ("ZMin", 30.0), ("ZMax", 500.0),
                 ("PHole", 0.02), ("HoleCellPx", 6.0), ("EdgeHolePx", 1.0), ("EdgeDispPx", 3.0), ("FattenPx", 0.0), ("Seed", 1.0)):
        g.scalar(n, d)
    names = ["MinMax", "UV", "Texel", "FocalPx", "BaselineCm", "SigmaDisp", "Quantize", "ZMin", "ZMax", "PHole", "HoleCellPx",
             "EdgeHolePx", "EdgeDispPx", "FattenPx", "Seed"]
    c = g.custom(DEGRADE_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, DEPTH_DEGRADE_USH, names, desc="DepthDegrade")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)

    mat, g = _render_target_material("M_TeleopCopyR")
    g.texture("Src")
    c = g.custom(COPY_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, None, ["Src", "UV"], desc="CopyR")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_checker():
    """M_TeleopChecker: lit 8 x 8 checkerboard (S5 translation-error measurement)."""
    mat = _new_material("M_TeleopChecker")
    g = _Graph(mat)
    g.node("UV", _expr(mat, unreal.MaterialExpressionTextureCoordinate, -1400, 1400))
    g.scalar("N", 8.0)
    c = g.custom(CHECKER_CODE, unreal.CustomMaterialOutputType.CMOT_FLOAT3, None, ["UV", "N"], desc="Checker")
    MEL.connect_material_property(c, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = _expr(mat, unreal.MaterialExpressionConstant, -300, 300)
    rough.set_editor_property("r", 0.8)
    MEL.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    _finish(mat)


def build_all():
    if not EAL.does_directory_exist(FOLDER):
        EAL.make_directory(FOLDER)
    build_unlit_color()
    build_lit_color()
    build_ghost()
    build_text_unlit()
    build_eye_test_pp()
    build_eye_test_vertex()
    build_present()
    build_depth_mesh()
    build_depth_passes()
    build_checker()


def report():
    """Print shader instruction counts (non-zero = compiled) for all Teleop materials."""
    for name in ("M_TeleopUnlitColor", "M_TeleopLitColor", "M_TeleopGhost", "M_TeleopTextUnlit", "M_EyeTest_PP", "M_EyeTest_Vertex", "M_TeleopPresent", "M_TeleopDepthMesh",
                 "M_TeleopDepthMinMax", "M_TeleopDepthDegrade", "M_TeleopCopyR", "M_TeleopChecker"):
        m = EAL.load_asset(f"{FOLDER}/{name}")
        if not m:
            unreal.log_warning(f"[teleop_materials] missing {name}")
            continue
        s = MEL.get_statistics(m)
        unreal.log(f"[teleop_materials] {name}: VS {s.num_vertex_shader_instructions} PS {s.num_pixel_shader_instructions} samplers {s.num_samplers}")
