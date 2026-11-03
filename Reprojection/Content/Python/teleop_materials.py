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
HUD_STENCIL = 1  # ATeleopOperatorPawn::HudStencilValue

PRESENT_CODE = f"""
// HUD and other room overlays (custom stencil {HUD_STENCIL}) stay visible.
if (abs(Stencil.r - {HUD_STENCIL}.0) < 0.5)
{{
    return SceneColor.rgb;
}}
float2 ViewportUV = SvPositionToViewportUV(Parameters.SvPosition);
float3 C = Teleop_PresentM0(ViewportUV, LeftTex, LeftTexSampler, RightTex, RightTexSampler, TanHalfFov.xy, Feather);
// Frames hold the capture's scene color, which is already pre-exposed like this view's scene color
// (both use the project's fixed exposure; measured M0 == GT brightness). ExposureScale is a manual trim.
return C * ExposureScale;
"""


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


def build_present():
    """M_TeleopPresent: post process (Scene Color Before Bloom) showing the robot frames per eye (M0)."""
    mat = _new_material("M_TeleopPresent")
    mat.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    mat.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_BLOOM)

    left = _texture_object_param(mat, "LeftFrame", -900, -200)
    right = _texture_object_param(mat, "RightFrame", -900, 0)
    tan = _expr(mat, unreal.MaterialExpressionVectorParameter, -900, 200)
    tan.set_editor_property("parameter_name", "TanHalfFov")
    tan.set_editor_property("default_value", unreal.LinearColor(1.428, 1.071, 0.0, 0.0))  # 110 deg, 4:3
    feather = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 400)
    feather.set_editor_property("parameter_name", "Feather")
    feather.set_editor_property("default_value", 0.01)
    exposure = _expr(mat, unreal.MaterialExpressionScalarParameter, -900, 500)
    exposure.set_editor_property("parameter_name", "ExposureScale")
    exposure.set_editor_property("default_value", 1.0)
    scene = _scene_texture(mat, unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0, -900, 650)
    stencil = _scene_texture(mat, unreal.SceneTextureId.PPI_CUSTOM_STENCIL, -900, 800)

    custom = _expr(mat, unreal.MaterialExpressionCustom, -400, 200)
    custom.set_editor_property("description", "TeleopPresent")
    custom.set_editor_property("code", PRESENT_CODE)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("include_file_paths", [PRESENT_USH])
    names = ["LeftTex", "RightTex", "TanHalfFov", "Feather", "ExposureScale", "SceneColor", "Stencil"]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property("input_name", n)
        inputs.append(ci)
    custom.set_editor_property("inputs", inputs)

    _connect(left, "", custom, ["LeftTex"])
    _connect(right, "", custom, ["RightTex"])
    _connect(tan, "", custom, ["TanHalfFov"])
    _connect(feather, "", custom, ["Feather"])
    _connect(exposure, "", custom, ["ExposureScale"])
    _connect(scene, "Color", custom, ["SceneColor"])
    _connect(stencil, "Color", custom, ["Stencil"])
    MEL.connect_material_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    _finish(mat)


def build_all():
    if not EAL.does_directory_exist(FOLDER):
        EAL.make_directory(FOLDER)
    build_unlit_color()
    build_lit_color()
    build_eye_test_pp()
    build_eye_test_vertex()
    build_present()
