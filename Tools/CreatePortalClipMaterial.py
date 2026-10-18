# Run once:
# UnrealEditor-Cmd.exe ActionDemo.uproject -ExecutePythonScript="E:/GitStuff/UnrealProjects/ActionDemo/Tools/CreatePortalClipMaterial.py"
import unreal

ASSET_PATH = "/Game/PortalGun/M_PortalObjectClip"
PACKAGE = "/Game/PortalGun"
NAME = "M_PortalObjectClip"


def main():
	if unreal.EditorAssetLibrary.does_asset_exist(ASSET_PATH):
		unreal.log("M_PortalObjectClip already exists")
		return

	factory = unreal.MaterialFactoryNew()
	asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
	mat = asset_tools.create_asset(NAME, PACKAGE, unreal.Material, factory)
	if not mat:
		unreal.log_error("Failed to create M_PortalObjectClip")
		return

	mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
	mat.set_editor_property("two_sided", True)

	unreal.MaterialEditingLibrary.delete_all_material_expressions(mat)

	# Base color
	base_color = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionVectorParameter, -500, -200)
	base_color.set_editor_property("parameter_name", "BaseColor")
	base_color.set_editor_property("default_value", unreal.LinearColor(0.2, 0.6, 1.0, 1.0))
	unreal.MaterialEditingLibrary.connect_material_property(
		base_color, "", unreal.MaterialProperty.MP_BASE_COLOR)

	# ClipEnabled (0/1)
	clip_enabled = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionScalarParameter, -900, 200)
	clip_enabled.set_editor_property("parameter_name", "ClipEnabled")
	clip_enabled.set_editor_property("default_value", 0.0)

	# ClipPlaneOrigin (xyz)
	clip_origin = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionVectorParameter, -900, 350)
	clip_origin.set_editor_property("parameter_name", "ClipPlaneOrigin")
	clip_origin.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

	# ClipPlaneNormal (xyz), points toward the visible side
	clip_normal = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionVectorParameter, -900, 500)
	clip_normal.set_editor_property("parameter_name", "ClipPlaneNormal")
	clip_normal.set_editor_property("default_value", unreal.LinearColor(1, 0, 0, 0))

	world_pos = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionWorldPosition, -900, 650)

	# (WorldPos - Origin) · Normal  → keep when >= 0 (front / visible side)
	subtract = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionSubtract, -650, 450)
	unreal.MaterialEditingLibrary.connect_material_expressions(world_pos, "", subtract, "A")
	unreal.MaterialEditingLibrary.connect_material_expressions(clip_origin, "", subtract, "B")

	dot = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionDotProduct, -450, 450)
	unreal.MaterialEditingLibrary.connect_material_expressions(subtract, "", dot, "A")
	unreal.MaterialEditingLibrary.connect_material_expressions(clip_normal, "", dot, "B")

	# If ClipEnabled < 0.5 → opacity 1, else opacity = (dot >= 0)
	one = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionConstant, -450, 200)
	one.set_editor_property("r", 1.0)

	zero = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionConstant, -450, 300)
	zero.set_editor_property("r", 0.0)

	# dot >= 0 → 1 else 0
	dot_mask = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionIf, -250, 450)
	unreal.MaterialEditingLibrary.connect_material_expressions(dot, "", dot_mask, "A")
	unreal.MaterialEditingLibrary.connect_material_expressions(zero, "", dot_mask, "B")
	unreal.MaterialEditingLibrary.connect_material_expressions(one, "", dot_mask, "AGreaterThanB")
	unreal.MaterialEditingLibrary.connect_material_expressions(one, "", dot_mask, "AEqualsB")
	unreal.MaterialEditingLibrary.connect_material_expressions(zero, "", dot_mask, "ALessThanB")

	half = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionConstant, -450, 100)
	half.set_editor_property("r", 0.5)

	enable_if = unreal.MaterialEditingLibrary.create_material_expression(
		mat, unreal.MaterialExpressionIf, -50, 250)
	unreal.MaterialEditingLibrary.connect_material_expressions(clip_enabled, "", enable_if, "A")
	unreal.MaterialEditingLibrary.connect_material_expressions(half, "", enable_if, "B")
	unreal.MaterialEditingLibrary.connect_material_expressions(dot_mask, "", enable_if, "AGreaterThanB")
	unreal.MaterialEditingLibrary.connect_material_expressions(one, "", enable_if, "AEqualsB")
	unreal.MaterialEditingLibrary.connect_material_expressions(one, "", enable_if, "ALessThanB")

	unreal.MaterialEditingLibrary.connect_material_property(
		enable_if, "", unreal.MaterialProperty.MP_OPACITY_MASK)

	unreal.MaterialEditingLibrary.recompile_material(mat)
	unreal.EditorAssetLibrary.save_asset(ASSET_PATH)
	unreal.log("Created " + ASSET_PATH)


if __name__ == "__main__":
	main()
