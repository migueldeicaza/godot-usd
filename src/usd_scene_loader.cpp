#include "usd_scene_loader.h"
#include "usd_curves.h"
#include "usd_light_proxy.h"
#include "usd_materials.h"
#include "usd_metadata.h"
#include "usd_mesh_builder.h"
#include "usd_skel.h"
#include "usd_stage_utils.h"
#include "usd_usdz.h"

#include <cstring>
#include <functional>
#include <unordered_map>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/animation.hpp>
#include <godot_cpp/classes/animation_player.hpp>
#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/scene_state.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/classes/spot_light3d.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/world_environment.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <pxr/base/gf/quatf.h>
#include <pxr/base/gf/vec3d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3h.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/payload.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/references.h>
#include <pxr/usd/usd/specializes.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdShade/tokens.h>
#include <pxr/usd/usdSkel/animation.h>
#include <pxr/usd/usdSkel/blendShape.h>
#include <pxr/usd/usdSkel/skeleton.h>

using namespace godot;
using namespace pxr;
using namespace godot_usd;

namespace {

constexpr const char *USD_STAGE_INSTANCE_GENERATED_META = "usd_stage_instance_generated";
constexpr const char *USD_PREVIEW_LIGHTING_MODE_SETTING = "filesystem/import/usd/preview_lighting_mode";

enum UsdPreviewLightingMode {
	USD_PREVIEW_LIGHTING_NEVER = 0,
	USD_PREVIEW_LIGHTING_WHEN_MISSING = 1,
	USD_PREVIEW_LIGHTING_ALWAYS = 2,
};

UsdPreviewLightingMode get_preview_lighting_mode() {
	ProjectSettings *project_settings = ProjectSettings::get_singleton();
	const int configured_mode = project_settings != nullptr ? (int)project_settings->get_setting(USD_PREVIEW_LIGHTING_MODE_SETTING, USD_PREVIEW_LIGHTING_WHEN_MISSING) : USD_PREVIEW_LIGHTING_WHEN_MISSING;

	switch (configured_mode) {
		case USD_PREVIEW_LIGHTING_NEVER:
			return USD_PREVIEW_LIGHTING_NEVER;
		case USD_PREVIEW_LIGHTING_ALWAYS:
			return USD_PREVIEW_LIGHTING_ALWAYS;
		default:
			return USD_PREVIEW_LIGHTING_WHEN_MISSING;
	}
}

String preview_lighting_mode_to_string(UsdPreviewLightingMode p_mode) {
	switch (p_mode) {
		case USD_PREVIEW_LIGHTING_NEVER:
			return "never";
		case USD_PREVIEW_LIGHTING_ALWAYS:
			return "always";
		default:
			return "when_missing";
	}
}

String make_valid_prim_identifier(const String &p_name) {
	std::string valid = TfMakeValidIdentifier(p_name.is_empty() ? "Node" : p_name.utf8().get_data());
	if (valid.empty()) {
		valid = "Node";
	}
	return to_godot_string(valid);
}

TfToken make_valid_prim_token(const String &p_name) {
	return TfToken(make_valid_prim_identifier(p_name).utf8().get_data());
}

UsdGeomSubset define_preserved_subset(const UsdGeomMesh &p_usd_mesh, const String &p_subset_path, const String &p_subset_name, const TfToken &p_element_type, const VtIntArray &p_indices, const TfToken &p_family_name, const TfToken &p_family_type) {
	SdfPath subset_path;
	if (!p_subset_path.is_empty()) {
		const SdfPath preferred_path(p_subset_path.utf8().get_data());
		if (preferred_path.IsAbsolutePath() && preferred_path.GetParentPath() == p_usd_mesh.GetPath()) {
			subset_path = preferred_path;
		}
	}
	if (subset_path.IsEmpty()) {
		String subset_name = to_godot_string(TfMakeValidIdentifier((p_subset_name.is_empty() ? String("GeomSubset") : p_subset_name).strip_edges().utf8().get_data()));
		if (subset_name.is_empty()) {
			subset_name = "GeomSubset";
		}
		subset_path = p_usd_mesh.GetPath().AppendChild(TfToken(subset_name.utf8().get_data()));
	}

	UsdGeomSubset subset = UsdGeomSubset::Define(p_usd_mesh.GetPrim().GetStage(), subset_path);
	if (!subset) {
		return UsdGeomSubset();
	}

	subset.GetElementTypeAttr().Set(p_element_type);
	subset.GetIndicesAttr().Set(p_indices);
	if (!p_family_name.IsEmpty()) {
		subset.CreateFamilyNameAttr().Set(p_family_name);
		UsdGeomSubset::SetFamilyType(UsdGeomImageable(p_usd_mesh.GetPrim()), p_family_name, p_family_type.IsEmpty() ? UsdGeomTokens->nonOverlapping : p_family_type);
	}
	return subset;
}

void apply_transform_components(const Vector3 &p_origin, const Quaternion &p_rotation, const Vector3 &p_scale, bool p_visible, UsdGeomXformable p_xformable) {
	if (!p_xformable) {
		return;
	}
	p_xformable.ClearXformOpOrder();
	p_xformable.AddTranslateOp(UsdGeomXformOp::PrecisionDouble).Set(GfVec3d(p_origin.x, p_origin.y, p_origin.z));
	p_xformable.AddOrientOp(UsdGeomXformOp::PrecisionFloat).Set(GfQuatf(p_rotation.w, GfVec3f(p_rotation.x, p_rotation.y, p_rotation.z)));
	p_xformable.AddScaleOp(UsdGeomXformOp::PrecisionFloat).Set(GfVec3f(p_scale.x, p_scale.y, p_scale.z));

	UsdGeomImageable imageable(p_xformable.GetPrim());
	if (imageable && !p_visible) {
		imageable.MakeInvisible();
	}
}

void apply_node3d_transform(const Node3D *p_node, UsdGeomXformable p_xformable) {
	ERR_FAIL_NULL(p_node);
	const Transform3D transform = p_node->get_transform();
	apply_transform_components(transform.origin, transform.basis.get_rotation_quaternion(), transform.basis.get_scale(), p_node->is_visible(), p_xformable);
}

Dictionary serialize_layer_offset_dict(const SdfLayerOffset &p_layer_offset) {
	Dictionary description;
	description["offset"] = p_layer_offset.GetOffset();
	description["scale"] = p_layer_offset.GetScale();
	return description;
}

void deserialize_layer_offset_dict(const Dictionary &p_description, SdfLayerOffset *r_layer_offset) {
	ERR_FAIL_NULL(r_layer_offset);
	const double offset = p_description.get("offset", 0.0);
	const double scale = p_description.get("scale", 1.0);
	*r_layer_offset = SdfLayerOffset(offset, scale);
}

bool deserialize_reference_dict(const Dictionary &p_description, SdfReference *r_reference) {
	ERR_FAIL_NULL_V(r_reference, false);
	const String asset_path = p_description.get("asset_path", String());
	const String prim_path = p_description.get("prim_path", String());
	SdfLayerOffset layer_offset;
	deserialize_layer_offset_dict(p_description.get("layer_offset", Dictionary()), &layer_offset);
	*r_reference = SdfReference(asset_path.utf8().get_data(), prim_path.is_empty() ? SdfPath() : SdfPath(prim_path.utf8().get_data()), layer_offset);
	return true;
}

bool deserialize_payload_dict(const Dictionary &p_description, SdfPayload *r_payload) {
	ERR_FAIL_NULL_V(r_payload, false);
	const String asset_path = p_description.get("asset_path", String());
	const String prim_path = p_description.get("prim_path", String());
	SdfLayerOffset layer_offset;
	deserialize_layer_offset_dict(p_description.get("layer_offset", Dictionary()), &layer_offset);
	*r_payload = SdfPayload(asset_path.utf8().get_data(), prim_path.is_empty() ? SdfPath() : SdfPath(prim_path.utf8().get_data()), layer_offset);
	return true;
}

SdfPathVector deserialize_path_array(const Array &p_paths) {
	SdfPathVector paths;
	for (int i = 0; i < p_paths.size(); i++) {
		if (p_paths[i].get_type() != Variant::STRING) {
			continue;
		}
		const String path = p_paths[i];
		if (path.is_empty()) {
			continue;
		}
		const SdfPath sdf_path(path.utf8().get_data());
		if (!sdf_path.IsEmpty()) {
			paths.push_back(sdf_path);
		}
	}
	return paths;
}

Error copy_file_absolute_preserving_contents(const String &p_source_absolute_path, const String &p_destination_absolute_path) {
	if (p_source_absolute_path.simplify_path() == p_destination_absolute_path.simplify_path()) {
		return OK;
	}

	const Error make_dir_error = DirAccess::make_dir_recursive_absolute(p_destination_absolute_path.get_base_dir());
	ERR_FAIL_COND_V_MSG(make_dir_error != OK, make_dir_error, vformat("Failed to create destination directory for USD save: %s", p_destination_absolute_path.get_base_dir()));

	return DirAccess::copy_absolute(p_source_absolute_path, p_destination_absolute_path);
}

bool variant_selections_match_stage_defaults(const Dictionary &p_variant_selections, const Dictionary &p_stage_variant_sets) {
	const Array prim_keys = p_variant_selections.keys();
	for (int prim_index = 0; prim_index < prim_keys.size(); prim_index++) {
		const String prim_path = prim_keys[prim_index];
		const Variant prim_selections_variant = p_variant_selections[prim_path];
		if (prim_selections_variant.get_type() != Variant::DICTIONARY) {
			return false;
		}

		const Variant prim_variant_sets_variant = p_stage_variant_sets.get(prim_path, Variant());
		if (prim_variant_sets_variant.get_type() != Variant::DICTIONARY) {
			return false;
		}

		const Dictionary prim_selections = prim_selections_variant;
		const Dictionary prim_variant_sets = prim_variant_sets_variant;
		const Array set_keys = prim_selections.keys();
		for (int set_index = 0; set_index < set_keys.size(); set_index++) {
			const String set_name = set_keys[set_index];
			const Variant selection = prim_selections[set_name];
			if (selection.get_type() != Variant::STRING && selection.get_type() != Variant::STRING_NAME) {
				return false;
			}

			const Variant set_description_variant = prim_variant_sets.get(set_name, Variant());
			if (set_description_variant.get_type() != Variant::DICTIONARY) {
				return false;
			}

			const Dictionary set_description = set_description_variant;
			if ((String)set_description.get("selection", String()) != (String)selection) {
				return false;
			}
		}
	}

	return true;
}

Error author_variant_selections_in_root_layer(const String &p_root_layer_absolute_path, const Dictionary &p_variant_selections) {
	if (p_variant_selections.is_empty()) {
		return OK;
	}

	SdfLayerRefPtr root_layer = SdfLayer::FindOrOpen(p_root_layer_absolute_path.utf8().get_data());
	ERR_FAIL_COND_V_MSG(!root_layer, ERR_CANT_OPEN, vformat("Failed to open USD root layer for variant authoring: %s", p_root_layer_absolute_path));

	UsdStageRefPtr stage = UsdStage::Open(root_layer);
	ERR_FAIL_COND_V_MSG(!stage, ERR_CANT_OPEN, vformat("Failed to compose USD root layer for variant authoring: %s", p_root_layer_absolute_path));

	stage->SetEditTarget(root_layer);
	apply_variant_selections_to_stage(stage, p_variant_selections);

	return root_layer->Save() ? OK : ERR_CANT_CREATE;
}

Error save_source_usd_layer_with_variant_defaults(const String &p_source_absolute_path, const String &p_destination_absolute_path, const String &p_destination_file_name, const Dictionary &p_variant_selections) {
	const String temp_directory = get_absolute_path("user://godot_usd_layer_save");
	const Error mkdir_error = DirAccess::make_dir_recursive_absolute(temp_directory);
	ERR_FAIL_COND_V_MSG(mkdir_error != OK, mkdir_error, "Failed to create temporary directory for USD layer save.");
	const String temp_layer_path = temp_directory.path_join(p_destination_file_name.is_empty() ? ("stage." + p_source_absolute_path.get_extension()) : p_destination_file_name);
	const Error copy_error = copy_file_absolute_preserving_contents(p_source_absolute_path, temp_layer_path);
	ERR_FAIL_COND_V_MSG(copy_error != OK, copy_error, vformat("Failed to copy source USD layer for variant save: %s", p_source_absolute_path));

	const Error author_error = author_variant_selections_in_root_layer(temp_layer_path, p_variant_selections);
	ERR_FAIL_COND_V_MSG(author_error != OK, author_error, vformat("Failed to author USD variant selections into layer: %s", temp_layer_path));

	return copy_file_absolute_preserving_contents(temp_layer_path, p_destination_absolute_path);
}

Error save_source_usdz_with_variant_defaults(const String &p_source_absolute_path, const String &p_destination_absolute_path, const String &p_destination_file_name, const Dictionary &p_variant_selections) {
	const String temp_directory = get_absolute_path("user://godot_usdz_save");
	const Error mkdir_error = DirAccess::make_dir_recursive_absolute(temp_directory);
	ERR_FAIL_COND_V_MSG(mkdir_error != OK, mkdir_error, "Failed to create temporary directory for USDZ save.");
	String root_layer_path;
	Vector<String> package_file_paths;
	const Error extract_error = extract_usdz_package(p_source_absolute_path, temp_directory, &root_layer_path, &package_file_paths);
	ERR_FAIL_COND_V_MSG(extract_error != OK, extract_error, vformat("Failed to extract source USDZ package for variant save: %s", p_source_absolute_path));

	const String root_layer_absolute_path = temp_directory.path_join(root_layer_path);
	const Error author_error = author_variant_selections_in_root_layer(root_layer_absolute_path, p_variant_selections);
	ERR_FAIL_COND_V_MSG(author_error != OK, author_error, vformat("Failed to author USDZ variant selections into root layer: %s", root_layer_path));

	const String temp_package_path = temp_directory.path_join(p_destination_file_name.is_empty() ? "stage.usdz" : p_destination_file_name);
	const Error package_error = create_usdz_package_from_extracted_files(temp_directory, package_file_paths, root_layer_path, temp_package_path);
	ERR_FAIL_COND_V_MSG(package_error != OK, package_error, vformat("Failed to create USDZ package with variant defaults: %s", p_destination_absolute_path));

	return copy_file_absolute_preserving_contents(temp_package_path, p_destination_absolute_path);
}

bool metadata_has_composition_arcs(const Dictionary &p_metadata) {
	return !((Array)p_metadata.get("usd:references", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:payloads", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:inherits", Array())).is_empty() ||
			!((Array)p_metadata.get("usd:specializes", Array())).is_empty();
}

bool usd_metadata_has_preserved_composition_boundary(const Dictionary &p_metadata) {
	return (bool)p_metadata.get("usd:variant_boundary", false) ||
			!((Array)p_metadata.get("usd:variant_context", Array())).is_empty() ||
			metadata_has_composition_arcs(p_metadata);
}

void report_usd_save_mode(const String &p_message, bool p_warning = false) {
	const String report = "USD save report: " + p_message;
	if (p_warning) {
		UtilityFunctions::push_warning(report);
	} else {
		UtilityFunctions::print(report);
	}
}

bool node_tree_has_composition_arcs(Node *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	const Dictionary metadata = get_usd_metadata(p_node);
	if (metadata_has_composition_arcs(metadata)) {
		return true;
	}
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		if (node_tree_has_composition_arcs(p_node->get_child(i, false))) {
			return true;
		}
	}
	return false;
}

bool node_tree_has_usd_composition_boundaries(Node *p_node) {
	ERR_FAIL_NULL_V(p_node, false);
	if (Object::cast_to<UsdStageInstance>(p_node) != nullptr) {
		return true;
	}

	const Dictionary metadata = get_usd_metadata(p_node);
	if (usd_metadata_has_preserved_composition_boundary(metadata)) {
		return true;
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		if (node_tree_has_usd_composition_boundaries(p_node->get_child(i, false))) {
			return true;
		}
	}

	return false;
}

bool node_tree_has_source_aware_static_data(Node *p_node) {
	ERR_FAIL_NULL_V(p_node, false);

	const Dictionary metadata = get_usd_metadata(p_node);
	if (Object::cast_to<Node3D>(p_node) != nullptr && metadata.has("usd:prim_path")) {
		return true;
	}
	if (Object::cast_to<Skeleton3D>(p_node) != nullptr && metadata.has("usd:prim_path")) {
		return true;
	}
	if (Object::cast_to<AnimationPlayer>(p_node) != nullptr) {
		const AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_node);
		const PackedStringArray animation_names = player->get_animation_list();
		for (int i = 0; i < animation_names.size(); i++) {
			const Ref<Animation> animation = player->get_animation(StringName(animation_names[i]));
			if (animation.is_valid() && get_usd_metadata(animation.ptr()).has("usd:animation_prim_path")) {
				return true;
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		if (node_tree_has_source_aware_static_data(p_node->get_child(i, false))) {
			return true;
		}
	}

	return false;
}

String get_imported_static_scene_source_path(Node *p_root) {
	ERR_FAIL_NULL_V(p_root, String());
	const Dictionary metadata = get_usd_metadata(p_root);
	return metadata.get("usd:source_path", String());
}

GfMatrix4d transform_to_gf_matrix(const Transform3D &p_transform) {
	GfMatrix4d matrix(1.0);
	const Vector3 x_axis = p_transform.basis.get_column(0);
	const Vector3 y_axis = p_transform.basis.get_column(1);
	const Vector3 z_axis = p_transform.basis.get_column(2);
	matrix[0][0] = x_axis.x;
	matrix[0][1] = x_axis.y;
	matrix[0][2] = x_axis.z;
	matrix[1][0] = y_axis.x;
	matrix[1][1] = y_axis.y;
	matrix[1][2] = y_axis.z;
	matrix[2][0] = z_axis.x;
	matrix[2][1] = z_axis.y;
	matrix[2][2] = z_axis.z;
	matrix[3][0] = p_transform.origin.x;
	matrix[3][1] = p_transform.origin.y;
	matrix[3][2] = p_transform.origin.z;
	return matrix;
}

GfQuatf godot_quat_to_gf_quat(const Quaternion &p_quaternion) {
	return GfQuatf((float)p_quaternion.w, GfVec3f((float)p_quaternion.x, (float)p_quaternion.y, (float)p_quaternion.z));
}

bool gf_matrix_is_equal_approx(const GfMatrix4d &p_left, const GfMatrix4d &p_right) {
	for (int row = 0; row < 4; row++) {
		for (int column = 0; column < 4; column++) {
			if (!Math::is_equal_approx((real_t)p_left[row][column], (real_t)p_right[row][column])) {
				return false;
			}
		}
	}
	return true;
}

bool gf_quat_is_equal_approx(const GfQuatf &p_left, const GfQuatf &p_right) {
	if (!Math::is_equal_approx((real_t)p_left.GetReal(), (real_t)p_right.GetReal())) {
		return false;
	}
	const GfVec3f left_imaginary = p_left.GetImaginary();
	const GfVec3f right_imaginary = p_right.GetImaginary();
	for (int i = 0; i < 3; i++) {
		if (!Math::is_equal_approx((real_t)left_imaginary[i], (real_t)right_imaginary[i])) {
			return false;
		}
	}
	return true;
}

bool gf_vec3f_is_equal_approx(const GfVec3f &p_left, const GfVec3f &p_right) {
	for (int i = 0; i < 3; i++) {
		if (!Math::is_equal_approx((real_t)p_left[i], (real_t)p_right[i])) {
			return false;
		}
	}
	return true;
}

bool gf_vec3h_is_equal_approx(const GfVec3h &p_left, const GfVec3h &p_right) {
	for (int i = 0; i < 3; i++) {
		if (!Math::is_equal_approx((real_t)p_left[i], (real_t)p_right[i])) {
			return false;
		}
	}
	return true;
}

GfVec3f godot_vector_to_gf_vec3f(const Vector3 &p_vector) {
	return GfVec3f((float)p_vector.x, (float)p_vector.y, (float)p_vector.z);
}

GfVec3h godot_vector_to_gf_vec3h(const Vector3 &p_vector) {
	return GfVec3h((float)p_vector.x, (float)p_vector.y, (float)p_vector.z);
}

String node_path_bone_name(const NodePath &p_path) {
	const String path_string = String(p_path);
	const int colon = path_string.rfind(":");
	if (colon < 0 || colon + 1 >= path_string.length()) {
		return String();
	}
	return path_string.substr(colon + 1);
}

String joint_leaf_name_for_save(const String &p_joint_path) {
	const int slash = p_joint_path.rfind("/");
	if (slash < 0 || slash + 1 >= p_joint_path.length()) {
		return p_joint_path;
	}
	return p_joint_path.substr(slash + 1);
}

String node_path_property_name(const NodePath &p_path) {
	const String path_string = String(p_path);
	const int colon = path_string.rfind(":");
	if (colon < 0 || colon + 1 >= path_string.length()) {
		return String();
	}
	return path_string.substr(colon + 1);
}

bool apply_skeleton_rest_edits_to_stage(Node *p_node, const UsdStageRefPtr &p_stage) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!p_stage, false);

	bool applied_any = false;
	if (Skeleton3D *skeleton = Object::cast_to<Skeleton3D>(p_node)) {
		const Dictionary metadata = get_usd_metadata(skeleton);
		const String prim_path = metadata.get("usd:prim_path", String());
		if (!prim_path.is_empty()) {
			const UsdPrim prim = p_stage->GetPrimAtPath(SdfPath(prim_path.utf8().get_data()));
			UsdSkelSkeleton usd_skeleton(prim);
			if (usd_skeleton) {
				VtArray<TfToken> joints;
				if (usd_skeleton.GetJointsAttr().Get(&joints) && !joints.empty()) {
					VtArray<GfMatrix4d> rest_transforms;
					if (!usd_skeleton.GetRestTransformsAttr().Get(&rest_transforms) || rest_transforms.size() != joints.size()) {
						rest_transforms.resize(joints.size());
					}
					bool changed = false;
					for (int bone_index = 0; bone_index < skeleton->get_bone_count(); bone_index++) {
						int joint_index = bone_index;
						if (skeleton->has_bone_meta(bone_index, StringName("usd_joint_index"))) {
							joint_index = (int)skeleton->get_bone_meta(bone_index, StringName("usd_joint_index"));
						}
						if (joint_index < 0 || joint_index >= (int)joints.size()) {
							continue;
						}
						const GfMatrix4d edited_rest_transform = transform_to_gf_matrix(skeleton->get_bone_rest(bone_index));
						if (!gf_matrix_is_equal_approx(rest_transforms[joint_index], edited_rest_transform)) {
							rest_transforms[joint_index] = edited_rest_transform;
							changed = true;
						}
					}
					if (changed) {
						usd_skeleton.GetRestTransformsAttr().Set(rest_transforms);
						applied_any = true;
					}
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		applied_any = apply_skeleton_rest_edits_to_stage(p_node->get_child(i, false), p_stage) || applied_any;
	}

	return applied_any;
}

bool apply_node3d_transform_edits_to_stage(Node *p_node, const UsdStageRefPtr &p_stage) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!p_stage, false);

	bool applied_any = false;
	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		const Dictionary metadata = get_usd_metadata(node_3d);
		const String prim_path = metadata.get("usd:prim_path", String());
		if (!prim_path.is_empty() && !(bool)metadata.get("usd:generated_preview", false)) {
			const UsdPrim prim = p_stage->GetPrimAtPath(SdfPath(prim_path.utf8().get_data()));
			UsdGeomXformable xformable(prim);
			if (xformable) {
				GfMatrix4d source_local_matrix(1.0);
				bool resets_xform_stack = false;
				xformable.GetLocalTransformation(&source_local_matrix, &resets_xform_stack, UsdTimeCode::Default());

				const GfMatrix4d edited_local_matrix = transform_to_gf_matrix(node_3d->get_transform());
				if (!gf_matrix_is_equal_approx(source_local_matrix, edited_local_matrix)) {
					apply_node3d_transform(node_3d, xformable);
					applied_any = true;
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		applied_any = apply_node3d_transform_edits_to_stage(p_node->get_child(i, false), p_stage) || applied_any;
	}

	return applied_any;
}

bool set_preview_surface_color_input_if_changed(UsdShadeShader p_shader, const TfToken &p_input_name, const Color &p_color) {
	if (!p_shader) {
		return false;
	}

	UsdShadeInput input = p_shader.GetInput(p_input_name);
	if (input && input.HasConnectedSource()) {
		return false;
	}

	const GfVec3f edited_value((float)p_color.r, (float)p_color.g, (float)p_color.b);
	GfVec3f source_value(1.0f, 1.0f, 1.0f);
	if (input && input.Get(&source_value) && gf_vec3f_is_equal_approx(source_value, edited_value)) {
		return false;
	}

	p_shader.CreateInput(p_input_name, SdfValueTypeNames->Color3f).Set(edited_value);
	return true;
}

bool set_preview_surface_float_input_if_changed(UsdShadeShader p_shader, const TfToken &p_input_name, float p_value, float p_default_value) {
	if (!p_shader) {
		return false;
	}

	UsdShadeInput input = p_shader.GetInput(p_input_name);
	if (input && input.HasConnectedSource()) {
		return false;
	}

	float source_value = p_default_value;
	if (input && input.Get(&source_value) && Math::is_equal_approx((real_t)source_value, (real_t)p_value)) {
		return false;
	}

	p_shader.CreateInput(p_input_name, SdfValueTypeNames->Float).Set(p_value);
	return true;
}

bool apply_preview_surface_material_edits_to_bound_material(const UsdShadeMaterial &p_material, const Ref<Material> &p_godot_material) {
	if (!p_material || p_godot_material.is_null()) {
		return false;
	}

	Ref<BaseMaterial3D> base_material = p_godot_material;
	if (base_material.is_null()) {
		return false;
	}

	UsdShadeShader preview_surface = UsdShadeShader::Get(p_material.GetPrim().GetStage(), p_material.GetPath().AppendChild(TfToken("PreviewSurface")));
	if (!preview_surface) {
		return false;
	}

	bool changed = false;
	const Color albedo = base_material->get_albedo();
	changed = set_preview_surface_color_input_if_changed(preview_surface, TfToken("diffuseColor"), albedo) || changed;
	changed = set_preview_surface_float_input_if_changed(preview_surface, TfToken("metallic"), base_material->get_metallic(), 0.0f) || changed;
	changed = set_preview_surface_float_input_if_changed(preview_surface, TfToken("roughness"), base_material->get_roughness(), 0.5f) || changed;
	changed = set_preview_surface_float_input_if_changed(preview_surface, TfToken("opacity"), CLAMP((float)albedo.a, 0.0f, 1.0f), 1.0f) || changed;
	return changed;
}

void append_static_save_warning(PackedStringArray *r_warnings, const String &p_warning) {
	if (r_warnings == nullptr || p_warning.is_empty()) {
		return;
	}
	for (int i = 0; i < r_warnings->size(); i++) {
		if ((*r_warnings)[i] == p_warning) {
			return;
		}
	}
	r_warnings->push_back(p_warning);
}

bool packed_int32_arrays_equal(const PackedInt32Array &p_left, const PackedInt32Array &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

bool packed_vector2_arrays_equal_approx(const PackedVector2Array &p_left, const PackedVector2Array &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (!p_left[i].is_equal_approx(p_right[i])) {
			return false;
		}
	}
	return true;
}

bool packed_vector3_arrays_equal_approx(const PackedVector3Array &p_left, const PackedVector3Array &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (!p_left[i].is_equal_approx(p_right[i])) {
			return false;
		}
	}
	return true;
}

bool packed_color_arrays_equal_approx(const PackedColorArray &p_left, const PackedColorArray &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (!p_left[i].is_equal_approx(p_right[i])) {
			return false;
		}
	}
	return true;
}

bool packed_byte_arrays_equal(const PackedByteArray &p_left, const PackedByteArray &p_right) {
	if (p_left.size() != p_right.size()) {
		return false;
	}
	for (int i = 0; i < p_left.size(); i++) {
		if (p_left[i] != p_right[i]) {
			return false;
		}
	}
	return true;
}

bool texture_images_equal(const Ref<Texture2D> &p_left, const Ref<Texture2D> &p_right) {
	if (p_left.is_null() && p_right.is_null()) {
		return true;
	}
	if (p_left.is_null() || p_right.is_null()) {
		return false;
	}

	Ref<Image> left_image = p_left->get_image();
	Ref<Image> right_image = p_right->get_image();
	if (left_image.is_null() && right_image.is_null()) {
		return true;
	}
	if (left_image.is_null() || right_image.is_null()) {
		return false;
	}
	if (left_image->get_width() != right_image->get_width() ||
			left_image->get_height() != right_image->get_height() ||
			left_image->get_format() != right_image->get_format()) {
		return false;
	}
	return packed_byte_arrays_equal(left_image->get_data(), right_image->get_data());
}

bool colors_equal_approx(const Color &p_left, const Color &p_right) {
	return p_left.is_equal_approx(p_right);
}

bool texture_network_material_differs(const Ref<Material> &p_current_material, const Ref<Material> &p_source_material) {
	Ref<BaseMaterial3D> current = p_current_material;
	Ref<BaseMaterial3D> source = p_source_material;
	if (current.is_null() || source.is_null()) {
		Dictionary current_sources;
		Dictionary source_sources;
		if (current.is_valid()) {
			current_sources = get_usd_metadata(current.ptr()).get("usd:preview_surface_texture_sources", Dictionary());
		}
		if (source.is_valid()) {
			source_sources = get_usd_metadata(source.ptr()).get("usd:preview_surface_texture_sources", Dictionary());
		}
		return (!current_sources.is_empty() || !source_sources.is_empty()) && current != source;
	}

	const Dictionary source_texture_sources = get_usd_metadata(source.ptr()).get("usd:preview_surface_texture_sources", Dictionary());
	const Dictionary current_texture_sources = get_usd_metadata(current.ptr()).get("usd:preview_surface_texture_sources", Dictionary());
	if (source_texture_sources.is_empty() && current_texture_sources.is_empty()) {
		return false;
	}

	if (!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_ALBEDO), source->get_texture(BaseMaterial3D::TEXTURE_ALBEDO)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_METALLIC), source->get_texture(BaseMaterial3D::TEXTURE_METALLIC)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS), source->get_texture(BaseMaterial3D::TEXTURE_ROUGHNESS)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_NORMAL), source->get_texture(BaseMaterial3D::TEXTURE_NORMAL)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_EMISSION), source->get_texture(BaseMaterial3D::TEXTURE_EMISSION)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_CLEARCOAT), source->get_texture(BaseMaterial3D::TEXTURE_CLEARCOAT)) ||
			!texture_images_equal(current->get_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION), source->get_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION))) {
		return true;
	}

	if (current->get_metallic_texture_channel() != source->get_metallic_texture_channel() ||
			current->get_roughness_texture_channel() != source->get_roughness_texture_channel() ||
			current->get_ao_texture_channel() != source->get_ao_texture_channel() ||
			!current->get_uv1_scale().is_equal_approx(source->get_uv1_scale()) ||
			!current->get_uv1_offset().is_equal_approx(source->get_uv1_offset())) {
		return true;
	}

	if (source_texture_sources.has("diffuseColor") && !colors_equal_approx(current->get_albedo(), source->get_albedo())) {
		return true;
	}
	if (source_texture_sources.has("metallic") && !Math::is_equal_approx(current->get_metallic(), source->get_metallic())) {
		return true;
	}
	if (source_texture_sources.has("roughness") && !Math::is_equal_approx(current->get_roughness(), source->get_roughness())) {
		return true;
	}
	if (source_texture_sources.has("emissiveColor") && !colors_equal_approx(current->get_emission(), source->get_emission())) {
		return true;
	}
	if ((source_texture_sources.has("clearcoat") || source_texture_sources.has("clearcoatRoughness")) &&
			(!Math::is_equal_approx(current->get_clearcoat(), source->get_clearcoat()) ||
					!Math::is_equal_approx(current->get_clearcoat_roughness(), source->get_clearcoat_roughness()))) {
		return true;
	}

	return false;
}

Ref<Material> get_effective_surface_material(MeshInstance3D *p_mesh_instance, const Ref<ArrayMesh> &p_mesh, int p_surface_index) {
	ERR_FAIL_NULL_V(p_mesh_instance, Ref<Material>());
	Ref<Material> material = p_mesh_instance->get_material_override();
	if (material.is_valid()) {
		return material;
	}
	if (p_mesh.is_valid() && p_surface_index >= 0 && p_surface_index < p_mesh->get_surface_count()) {
		return p_mesh->surface_get_material(p_surface_index);
	}
	return Ref<Material>();
}

String get_surface_material_path(const Ref<Material> &p_material) {
	if (p_material.is_null()) {
		return String();
	}
	return get_usd_metadata(p_material.ptr()).get("usd:material_path", String());
}

Dictionary get_surface_description(const Array &p_descriptions, int p_surface_index) {
	if (p_surface_index < 0 || p_surface_index >= p_descriptions.size() || p_descriptions[p_surface_index].get_type() != Variant::DICTIONARY) {
		return Dictionary();
	}
	return p_descriptions[p_surface_index];
}

void append_mesh_array_unsupported_warning(const String &p_prim_path, int p_surface_index, const String &p_kind, PackedStringArray *r_warnings) {
	append_static_save_warning(r_warnings, vformat("%s changed at %s surface %d; source-aware static save currently only merges point positions", p_kind, p_prim_path, p_surface_index));
}

void warn_unsupported_static_mesh_edits(Node *p_node, const UsdStageRefPtr &p_stage, PackedStringArray *r_warnings) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_COND(p_stage == nullptr);

	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		const Dictionary metadata = get_usd_metadata(mesh_instance);
		const String prim_path = metadata.get("usd:prim_path", String());
		Ref<ArrayMesh> current_mesh = mesh_instance->get_mesh();
		if (!prim_path.is_empty() && current_mesh.is_valid()) {
			UsdGeomMesh usd_mesh(p_stage->GetPrimAtPath(SdfPath(prim_path.utf8().get_data())));
			if (usd_mesh) {
				Dictionary source_mapping_notes;
				const MeshBuildResult source_result = build_polygon_mesh(p_stage, UsdTimeCode::Default(), usd_mesh, &source_mapping_notes);
				Ref<ArrayMesh> source_mesh = source_result.mesh;
				if (source_mesh.is_valid()) {
					if (current_mesh->get_surface_count() != source_mesh->get_surface_count()) {
						append_static_save_warning(r_warnings, vformat("mesh surface topology changed at %s: source had %d surfaces but edited mesh has %d", prim_path, source_mesh->get_surface_count(), current_mesh->get_surface_count()));
					}

					const int surface_count = MIN(current_mesh->get_surface_count(), source_mesh->get_surface_count());
					for (int surface_index = 0; surface_index < surface_count; surface_index++) {
						const Array current_arrays = current_mesh->surface_get_arrays(surface_index);
						const Array source_arrays = source_mesh->surface_get_arrays(surface_index);
						if (current_arrays.size() != Mesh::ARRAY_MAX || source_arrays.size() != Mesh::ARRAY_MAX) {
							continue;
						}

						const PackedInt32Array current_indices = current_arrays[Mesh::ARRAY_INDEX];
						const PackedInt32Array source_indices = source_arrays[Mesh::ARRAY_INDEX];
						if (!packed_int32_arrays_equal(current_indices, source_indices)) {
							append_mesh_array_unsupported_warning(prim_path, surface_index, "mesh index buffer", r_warnings);
						}

						const PackedVector3Array current_normals = current_arrays[Mesh::ARRAY_NORMAL];
						const PackedVector3Array source_normals = source_arrays[Mesh::ARRAY_NORMAL];
						if (!packed_vector3_arrays_equal_approx(current_normals, source_normals)) {
							append_mesh_array_unsupported_warning(prim_path, surface_index, "mesh normal primvar", r_warnings);
						}

						const PackedVector2Array current_uvs = current_arrays[Mesh::ARRAY_TEX_UV];
						const PackedVector2Array source_uvs = source_arrays[Mesh::ARRAY_TEX_UV];
						if (!packed_vector2_arrays_equal_approx(current_uvs, source_uvs)) {
							append_mesh_array_unsupported_warning(prim_path, surface_index, "mesh UV primvar", r_warnings);
						}

						const PackedColorArray current_colors = current_arrays[Mesh::ARRAY_COLOR];
						const PackedColorArray source_colors = source_arrays[Mesh::ARRAY_COLOR];
						if (!packed_color_arrays_equal_approx(current_colors, source_colors)) {
							append_mesh_array_unsupported_warning(prim_path, surface_index, "mesh displayColor primvar", r_warnings);
						}

						const Dictionary source_description = get_surface_description(source_result.material_subsets, surface_index);
						const String source_material_path = source_description.get("material_path", String());
						const Ref<Material> current_material = get_effective_surface_material(mesh_instance, current_mesh, surface_index);
						const Ref<Material> source_material = source_mesh->surface_get_material(surface_index);
						const String current_material_path = get_surface_material_path(current_material);
						if (!source_material_path.is_empty() && (current_material.is_null() || current_material_path != source_material_path)) {
							append_static_save_warning(r_warnings, vformat("material subset rebinding changed at %s surface %d; source-aware static save currently preserves authored subset bindings", prim_path, surface_index));
						}
						if (texture_network_material_differs(current_material, source_material)) {
							append_static_save_warning(r_warnings, vformat("texture-network material edit changed at %s surface %d; source-aware static save currently preserves authored texture networks", prim_path, surface_index));
						}
					}
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		warn_unsupported_static_mesh_edits(p_node->get_child(i, false), p_stage, r_warnings);
	}
}

bool apply_material_edits_to_stage(Node *p_node, const UsdStageRefPtr &p_stage, PackedStringArray *r_warnings) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!p_stage, false);

	bool applied_any = false;
	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		const Dictionary metadata = get_usd_metadata(mesh_instance);
		const String prim_path = metadata.get("usd:prim_path", String());
		if (!prim_path.is_empty()) {
			const UsdPrim prim = p_stage->GetPrimAtPath(SdfPath(prim_path.utf8().get_data()));
			if (prim) {
				UsdShadeMaterial bound_material = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
				Ref<Material> material = mesh_instance->get_material_override();
				if (material.is_null()) {
					material = mesh_instance->get_active_material(0);
				}
				if (material.is_valid() && bound_material) {
					const String material_path = get_usd_metadata(material.ptr()).get("usd:material_path", String());
					const String bound_material_path = to_godot_string(bound_material.GetPath().GetString());
					if (material_path.is_empty() || material_path != bound_material_path) {
						append_static_save_warning(r_warnings, vformat("material replacement/rebinding changed at %s; source-aware static save currently only merges in-place edits to the originally bound material", prim_path));
					} else {
						applied_any = apply_preview_surface_material_edits_to_bound_material(bound_material, material) || applied_any;
					}
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		applied_any = apply_material_edits_to_stage(p_node->get_child(i, false), p_stage, r_warnings) || applied_any;
	}

	return applied_any;
}

bool apply_mesh_point_edits_to_stage(Node *p_node, const UsdStageRefPtr &p_stage, PackedStringArray *r_warnings) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!p_stage, false);

	bool applied_any = false;
	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		const Dictionary metadata = get_usd_metadata(mesh_instance);
		const String prim_path = metadata.get("usd:prim_path", String());
		Ref<ArrayMesh> array_mesh = mesh_instance->get_mesh();
		if (!prim_path.is_empty() && array_mesh.is_valid()) {
			UsdGeomMesh usd_mesh(p_stage->GetPrimAtPath(SdfPath(prim_path.utf8().get_data())));
			if (usd_mesh) {
				VtArray<GfVec3f> points;
				if (usd_mesh.GetPointsAttr().Get(&points) && !points.empty()) {
					std::vector<bool> seen_points(points.size(), false);
					std::vector<GfVec3f> edited_points(points.begin(), points.end());
					bool can_merge = true;
					String unsupported_reason;
					int mapped_vertex_count = 0;
					const Array surface_descriptions = metadata.get("usd:material_subsets", Array());

					for (int surface_index = 0; surface_index < array_mesh->get_surface_count() && can_merge; surface_index++) {
						const Array arrays = array_mesh->surface_get_arrays(surface_index);
						if (arrays.size() != Mesh::ARRAY_MAX) {
							continue;
						}
						const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
						if (vertices.is_empty()) {
							continue;
						}

						PackedInt32Array authored_point_indices;
						if (surface_index < surface_descriptions.size() && surface_descriptions[surface_index].get_type() == Variant::DICTIONARY) {
							const Dictionary description = surface_descriptions[surface_index];
							authored_point_indices = description.get("authored_point_indices", PackedInt32Array());
						}

						if (authored_point_indices.size() != vertices.size()) {
							if (array_mesh->get_surface_count() == 1 && vertices.size() == (int)points.size()) {
								authored_point_indices.resize(vertices.size());
								for (int i = 0; i < vertices.size(); i++) {
									authored_point_indices.set(i, i);
								}
							} else {
								unsupported_reason = vformat("mesh topology changed at %s: surface %d has %d vertices, but source point mapping has %d entries", prim_path, surface_index, vertices.size(), authored_point_indices.size());
								can_merge = false;
								break;
							}
						}

						for (int vertex_index = 0; vertex_index < vertices.size(); vertex_index++) {
							const int point_index = authored_point_indices[vertex_index];
							if (point_index < 0 || point_index >= (int)points.size()) {
								unsupported_reason = vformat("mesh point mapping changed at %s: surface %d references invalid source point %d", prim_path, surface_index, point_index);
								can_merge = false;
								break;
							}
							const Vector3 vertex = vertices[vertex_index];
							const GfVec3f edited_point((float)vertex.x, (float)vertex.y, (float)vertex.z);
							if (seen_points[point_index] && !gf_vec3f_is_equal_approx(edited_points[point_index], edited_point)) {
								unsupported_reason = vformat("mesh topology/vertex split changed at %s: source point %d maps to conflicting edited vertices", prim_path, point_index);
								can_merge = false;
								break;
							}
							seen_points[point_index] = true;
							edited_points[point_index] = edited_point;
							mapped_vertex_count++;
						}
					}

					if (can_merge && mapped_vertex_count > 0) {
						bool changed = false;
						for (int point_index = 0; point_index < (int)points.size(); point_index++) {
							if (seen_points[point_index] && !gf_vec3f_is_equal_approx(points[point_index], edited_points[point_index])) {
								points[point_index] = edited_points[point_index];
								changed = true;
							}
						}
						if (changed) {
							usd_mesh.GetPointsAttr().Set(points);
							applied_any = true;
						}
					} else if (!unsupported_reason.is_empty()) {
						append_static_save_warning(r_warnings, unsupported_reason);
					}
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		applied_any = apply_mesh_point_edits_to_stage(p_node->get_child(i, false), p_stage, r_warnings) || applied_any;
	}

	return applied_any;
}

bool apply_animation_edits_to_stage(Node *p_node, const UsdStageRefPtr &p_stage, PackedStringArray *r_warnings) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(!p_stage, false);

	bool applied_any = false;
	if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_node)) {
		const PackedStringArray animation_names = player->get_animation_list();
		for (int animation_index = 0; animation_index < animation_names.size(); animation_index++) {
			const Ref<Animation> animation = player->get_animation(StringName(animation_names[animation_index]));
			if (animation.is_null()) {
				continue;
			}

			const Dictionary metadata = get_usd_metadata(animation.ptr());
			const String animation_prim_path = metadata.get("usd:animation_prim_path", String());
			const Array joint_paths = metadata.get("usd:joint_paths", Array());
			if (animation_prim_path.is_empty()) {
				continue;
			}

			UsdSkelAnimation usd_animation(p_stage->GetPrimAtPath(SdfPath(animation_prim_path.utf8().get_data())));
			if (!usd_animation) {
				continue;
			}

			UsdAttribute translations_attr = usd_animation.GetTranslationsAttr();
			UsdAttribute rotations_attr = usd_animation.GetRotationsAttr();
			UsdAttribute scales_attr = usd_animation.GetScalesAttr();
			UsdAttribute blend_shape_weights_attr = usd_animation.GetBlendShapeWeightsAttr();

			std::unordered_map<std::string, int> joint_index_by_bone_name;
			for (int joint_index = 0; joint_index < joint_paths.size(); joint_index++) {
				const String joint_path = joint_paths[joint_index];
				const String bone_name = joint_leaf_name_for_save(joint_path);
				if (!bone_name.is_empty()) {
					joint_index_by_bone_name[bone_name.utf8().get_data()] = joint_index;
				}
			}

			const double time_codes_per_second = MAX((double)metadata.get("usd:time_codes_per_second", p_stage->GetTimeCodesPerSecond()), 1.0);
			const double start_time = (double)metadata.get("usd:start_time_code", 0.0);
			bool has_derived_inbetween_blend_shape_tracks = false;
			for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
				if (animation->track_get_type(track_index) != Animation::TYPE_BLEND_SHAPE) {
					continue;
				}
				const String blend_shape_name = node_path_property_name(animation->track_get_path(track_index));
				if (blend_shape_name.contains("__inbetween__")) {
					has_derived_inbetween_blend_shape_tracks = true;
					append_static_save_warning(r_warnings, vformat("derived inbetween blend-shape track at %s:%s is unsupported; source-aware static save currently only merges direct primary blend-shape weights", animation_prim_path, blend_shape_name));
				}
			}
			for (int track_index = 0; track_index < animation->get_track_count(); track_index++) {
				if (animation->track_get_type(track_index) == Animation::TYPE_BLEND_SHAPE) {
					if (has_derived_inbetween_blend_shape_tracks) {
						continue;
					}
					const Array blend_shape_names = metadata.get("usd:blend_shape_names", Array());
					const String blend_shape_name = node_path_property_name(animation->track_get_path(track_index));
					int blend_shape_index = -1;
					for (int i = 0; i < blend_shape_names.size(); i++) {
						if ((String)blend_shape_names[i] == blend_shape_name) {
							blend_shape_index = i;
							break;
						}
					}
					if (blend_shape_index < 0 || !blend_shape_weights_attr) {
						continue;
					}

					for (int key_index = 0; key_index < animation->track_get_key_count(track_index); key_index++) {
						const Variant value = animation->track_get_key_value(track_index, key_index);
						if (value.get_type() != Variant::FLOAT && value.get_type() != Variant::INT) {
							continue;
						}
						const double sample_time = start_time + animation->track_get_key_time(track_index, key_index) * time_codes_per_second;
						VtArray<float> blend_shape_weights;
						if (!blend_shape_weights_attr.Get(&blend_shape_weights, sample_time) || blend_shape_weights.size() != (size_t)blend_shape_names.size()) {
							continue;
						}
						const float edited_weight = (float)value;
						if (!Math::is_equal_approx((real_t)blend_shape_weights[blend_shape_index], (real_t)edited_weight)) {
							blend_shape_weights[blend_shape_index] = edited_weight;
							blend_shape_weights_attr.Set(blend_shape_weights, sample_time);
							applied_any = true;
						}
					}
					continue;
				}

				const String bone_name = node_path_bone_name(animation->track_get_path(track_index));
				const auto joint_it = joint_index_by_bone_name.find(bone_name.utf8().get_data());
				if (joint_it == joint_index_by_bone_name.end()) {
					continue;
				}
				const int joint_index = joint_it->second;

				for (int key_index = 0; key_index < animation->track_get_key_count(track_index); key_index++) {
					const Variant value = animation->track_get_key_value(track_index, key_index);
					const double sample_time = start_time + animation->track_get_key_time(track_index, key_index) * time_codes_per_second;

					if (animation->track_get_type(track_index) == Animation::TYPE_POSITION_3D && translations_attr && value.get_type() == Variant::VECTOR3) {
						VtArray<GfVec3f> translations;
						if (!translations_attr.Get(&translations, sample_time) || translations.size() != (size_t)joint_paths.size()) {
							continue;
						}
						const GfVec3f edited_translation = godot_vector_to_gf_vec3f((Vector3)value);
						if (!gf_vec3f_is_equal_approx(translations[joint_index], edited_translation)) {
							translations[joint_index] = edited_translation;
							translations_attr.Set(translations, sample_time);
							applied_any = true;
						}
					} else if (animation->track_get_type(track_index) == Animation::TYPE_ROTATION_3D && rotations_attr && value.get_type() == Variant::QUATERNION) {
						VtArray<GfQuatf> rotations;
						if (!rotations_attr.Get(&rotations, sample_time) || rotations.size() != (size_t)joint_paths.size()) {
							continue;
						}
						const GfQuatf edited_rotation = godot_quat_to_gf_quat((Quaternion)value);
						if (!gf_quat_is_equal_approx(rotations[joint_index], edited_rotation)) {
							rotations[joint_index] = edited_rotation;
							rotations_attr.Set(rotations, sample_time);
							applied_any = true;
						}
					} else if (animation->track_get_type(track_index) == Animation::TYPE_SCALE_3D && scales_attr && value.get_type() == Variant::VECTOR3) {
						VtArray<GfVec3h> scales;
						if (!scales_attr.Get(&scales, sample_time) || scales.size() != (size_t)joint_paths.size()) {
							continue;
						}
						const GfVec3h edited_scale = godot_vector_to_gf_vec3h((Vector3)value);
						if (!gf_vec3h_is_equal_approx(scales[joint_index], edited_scale)) {
							scales[joint_index] = edited_scale;
							scales_attr.Set(scales, sample_time);
							applied_any = true;
						}
					}
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		applied_any = apply_animation_edits_to_stage(p_node->get_child(i, false), p_stage, r_warnings) || applied_any;
	}

	return applied_any;
}

Error save_static_imported_data_to_source_copy(Node *p_root, const String &p_source_absolute_path, const String &p_destination_absolute_path) {
	ERR_FAIL_NULL_V(p_root, ERR_INVALID_PARAMETER);

	const Error copy_error = copy_file_absolute_preserving_contents(p_source_absolute_path, p_destination_absolute_path);
	ERR_FAIL_COND_V_MSG(copy_error != OK, copy_error, vformat("Failed to copy source USD layer for source-aware static save: %s", p_source_absolute_path));

	SdfLayerRefPtr root_layer = SdfLayer::FindOrOpen(p_destination_absolute_path.utf8().get_data());
	ERR_FAIL_COND_V_MSG(!root_layer, ERR_CANT_OPEN, vformat("Failed to open copied USD layer for source-aware static save: %s", p_destination_absolute_path));

	UsdStageRefPtr stage = UsdStage::Open(root_layer);
	ERR_FAIL_COND_V_MSG(!stage, ERR_CANT_OPEN, vformat("Failed to compose copied USD layer for source-aware static save: %s", p_destination_absolute_path));
	UsdStageRefPtr source_stage = UsdStage::Open(p_source_absolute_path.utf8().get_data());
	if (!source_stage) {
		source_stage = stage;
	}

	stage->SetEditTarget(root_layer);
	PackedStringArray unsupported_warnings;
	warn_unsupported_static_mesh_edits(p_root, source_stage, &unsupported_warnings);
	const bool applied_transform_edits = apply_node3d_transform_edits_to_stage(p_root, stage);
	const bool applied_material_edits = apply_material_edits_to_stage(p_root, stage, &unsupported_warnings);
	const bool applied_mesh_point_edits = apply_mesh_point_edits_to_stage(p_root, stage, &unsupported_warnings);
	const bool applied_skeleton_edits = apply_skeleton_rest_edits_to_stage(p_root, stage);
	const bool applied_animation_edits = apply_animation_edits_to_stage(p_root, stage, &unsupported_warnings);
	for (int i = 0; i < unsupported_warnings.size(); i++) {
		UtilityFunctions::push_warning(vformat("USD source-aware static save could not merge unsupported edit: %s", unsupported_warnings[i]));
	}
	if (!applied_transform_edits && !applied_material_edits && !applied_mesh_point_edits && !applied_skeleton_edits && !applied_animation_edits) {
		return OK;
	}

	return root_layer->Save() ? OK : ERR_CANT_CREATE;
}

void reapply_composition_arcs(const UsdPrim &p_prim, const Object *p_source_object) {
	ERR_FAIL_NULL(p_source_object);
	if (!p_prim) {
		return;
	}

	const Dictionary metadata = get_usd_metadata(p_source_object);

	const Array references = metadata.get("usd:references", Array());
	if (!references.is_empty()) {
		SdfReferenceVector reference_items;
		for (int i = 0; i < references.size(); i++) {
			if (references[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			SdfReference reference;
			if (deserialize_reference_dict(references[i], &reference)) {
				reference_items.push_back(reference);
			}
		}
		if (!reference_items.empty()) {
			UsdReferences usd_references = p_prim.GetReferences();
			if (usd_references) {
				usd_references.SetReferences(reference_items);
			}
		}
	}

	const Array payloads = metadata.get("usd:payloads", Array());
	if (!payloads.is_empty()) {
		SdfPayloadVector payload_items;
		for (int i = 0; i < payloads.size(); i++) {
			if (payloads[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			SdfPayload payload;
			if (deserialize_payload_dict(payloads[i], &payload)) {
				payload_items.push_back(payload);
			}
		}
		if (!payload_items.empty()) {
			UsdPayloads usd_payloads = p_prim.GetPayloads();
			if (usd_payloads) {
				usd_payloads.SetPayloads(payload_items);
			}
		}
	}

	const Array inherits = metadata.get("usd:inherits", Array());
	if (!inherits.is_empty()) {
		const SdfPathVector inherit_items = deserialize_path_array(inherits);
		if (!inherit_items.empty()) {
			UsdInherits usd_inherits = p_prim.GetInherits();
			if (usd_inherits) {
				usd_inherits.SetInherits(inherit_items);
			}
		}
	}

	const Array specializes = metadata.get("usd:specializes", Array());
	if (!specializes.is_empty()) {
		const SdfPathVector specialize_items = deserialize_path_array(specializes);
		if (!specialize_items.empty()) {
			UsdSpecializes usd_specializes = p_prim.GetSpecializes();
			if (usd_specializes) {
				usd_specializes.SetSpecializes(specialize_items);
			}
		}
	}
}

void apply_source_prim_transform(const UsdPrim &p_source_prim, UsdPrim p_target_prim) {
	if (!p_source_prim || !p_target_prim) {
		return;
	}

	UsdGeomXformable source_xformable(p_source_prim);
	UsdGeomXformable target_xformable(p_target_prim);
	if (!source_xformable || !target_xformable) {
		return;
	}

	GfMatrix4d local_matrix(1.0);
	bool resets_xform_stack = false;
	if (!source_xformable.GetLocalTransformation(&local_matrix, &resets_xform_stack, UsdTimeCode::Default())) {
		return;
	}

	target_xformable.ClearXformOpOrder();
	target_xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble).Set(local_matrix);
}

bool transforms_equal_approx(const Transform3D &p_left, const Transform3D &p_right) {
	if (!p_left.origin.is_equal_approx(p_right.origin)) {
		return false;
	}
	for (int i = 0; i < 3; i++) {
		if (!p_left.basis.get_column(i).is_equal_approx(p_right.basis.get_column(i))) {
			return false;
		}
	}
	return true;
}

bool generated_node_state_matches(Node *p_current, Node *p_expected, String *r_reason) {
	ERR_FAIL_NULL_V(p_current, false);
	ERR_FAIL_NULL_V(p_expected, false);

	if (p_current->get_name() != p_expected->get_name()) {
		if (r_reason != nullptr) {
			*r_reason = vformat("name changed from '%s' to '%s'", p_expected->get_name(), p_current->get_name());
		}
		return false;
	}

	if (String(p_current->get_class()) != String(p_expected->get_class())) {
		if (r_reason != nullptr) {
			*r_reason = vformat("node type changed from '%s' to '%s'", p_expected->get_class(), p_current->get_class());
		}
		return false;
	}

	Node3D *current_3d = Object::cast_to<Node3D>(p_current);
	Node3D *expected_3d = Object::cast_to<Node3D>(p_expected);
	if ((current_3d == nullptr) != (expected_3d == nullptr)) {
		if (r_reason != nullptr) {
			*r_reason = "Node3D mapping changed";
		}
		return false;
	}
	if (current_3d != nullptr && expected_3d != nullptr) {
		if (!transforms_equal_approx(current_3d->get_transform(), expected_3d->get_transform())) {
			if (r_reason != nullptr) {
				*r_reason = "transform changed";
			}
			return false;
		}
		if (current_3d->is_visible() != expected_3d->is_visible()) {
			if (r_reason != nullptr) {
				*r_reason = "visibility changed";
			}
			return false;
		}
	}

	MeshInstance3D *current_mesh = Object::cast_to<MeshInstance3D>(p_current);
	MeshInstance3D *expected_mesh = Object::cast_to<MeshInstance3D>(p_expected);
	if ((current_mesh == nullptr) != (expected_mesh == nullptr)) {
		if (r_reason != nullptr) {
			*r_reason = "mesh node mapping changed";
		}
		return false;
	}
	if (current_mesh != nullptr && expected_mesh != nullptr) {
		const Ref<Mesh> current_mesh_resource = current_mesh->get_mesh();
		const Ref<Mesh> expected_mesh_resource = expected_mesh->get_mesh();
		const int current_surface_count = current_mesh_resource.is_valid() ? current_mesh_resource->get_surface_count() : 0;
		const int expected_surface_count = expected_mesh_resource.is_valid() ? expected_mesh_resource->get_surface_count() : 0;
		if (current_surface_count != expected_surface_count) {
			if (r_reason != nullptr) {
				*r_reason = "mesh surface count changed";
			}
			return false;
		}
	}

	return true;
}

void collect_generated_composition_boundary_nodes(Node *p_node, bool p_under_composition_boundary, std::unordered_map<std::string, Node *> *r_nodes, PackedStringArray *r_unmapped_nodes, int p_unmapped_limit = 12) {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_nodes);

	const Dictionary metadata = get_usd_metadata(p_node);
	if ((bool)metadata.get("usd:generated_preview", false)) {
		return;
	}

	const bool under_composition_boundary = p_under_composition_boundary || usd_metadata_has_preserved_composition_boundary(metadata);
	const String prim_path = metadata.get("usd:prim_path", String());

	if (under_composition_boundary) {
		if (!prim_path.is_empty()) {
			(*r_nodes)[std::string(prim_path.utf8().get_data())] = p_node;
		} else if (r_unmapped_nodes != nullptr && r_unmapped_nodes->size() < p_unmapped_limit) {
			r_unmapped_nodes->push_back(String(p_node->get_path()));
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		collect_generated_composition_boundary_nodes(p_node->get_child(i, false), under_composition_boundary, r_nodes, r_unmapped_nodes, p_unmapped_limit);
	}
}


bool export_composition_preserving_node(Node *p_node, const UsdStageRefPtr &p_stage, const UsdStageRefPtr &p_source_stage) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(p_stage == nullptr, false);

	if (p_node->has_meta(StringName("usd")) && (bool)get_usd_metadata(p_node).get("usd:generated_preview", false)) {
		return true;
	}

	const Dictionary metadata = get_usd_metadata(p_node);
	const String prim_path_string = metadata.get("usd:prim_path", String());
	if (prim_path_string.is_empty()) {
		for (int i = 0; i < p_node->get_child_count(false); i++) {
			if (!export_composition_preserving_node(p_node->get_child(i, false), p_stage, p_source_stage)) {
				return false;
			}
		}
		return true;
	}

	const SdfPath prim_path(prim_path_string.utf8().get_data());
	if (prim_path.IsEmpty()) {
		return false;
	}

	const bool has_references = !((Array)metadata.get("usd:references", Array())).is_empty();
	const bool has_payloads = !((Array)metadata.get("usd:payloads", Array())).is_empty();
	const bool has_inherits = !((Array)metadata.get("usd:inherits", Array())).is_empty();
	const bool has_specializes = !((Array)metadata.get("usd:specializes", Array())).is_empty();

	UsdPrim prim;
	if (has_references || has_payloads) {
		prim = p_stage->OverridePrim(prim_path);
	} else if (Object::cast_to<Node3D>(p_node) != nullptr) {
		prim = UsdGeomXform::Define(p_stage, prim_path).GetPrim();
	} else {
		prim = p_stage->DefinePrim(prim_path);
	}
	if (!prim) {
		return false;
	}

	const UsdPrim source_prim = p_source_stage != nullptr ? p_source_stage->GetPrimAtPath(prim_path) : UsdPrim();
	if (source_prim) {
		apply_source_prim_transform(source_prim, prim);
	} else if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		UsdGeomXformable xformable(prim);
		apply_node3d_transform(node_3d, xformable);
	}

	if (has_references || has_payloads || has_inherits || has_specializes) {
		reapply_composition_arcs(prim, p_node);
	}

	if (has_references || has_payloads) {
		return true;
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		if (!export_composition_preserving_node(p_node->get_child(i, false), p_stage, p_source_stage)) {
			return false;
		}
	}
	return true;
}

Error save_composition_preserving_generated_scene(Node *p_generated_root, const String &p_path, const UsdStageRefPtr &p_source_stage) {
	ERR_FAIL_NULL_V(p_generated_root, ERR_INVALID_PARAMETER);

	const String absolute_path = get_absolute_path(p_path);
	const String base_dir = absolute_path.get_base_dir();
	if (!base_dir.is_empty()) {
		const Error mkdir_error = DirAccess::make_dir_recursive_absolute(base_dir);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
	}

	UsdStageRefPtr stage = UsdStage::CreateNew(absolute_path.utf8().get_data());
	if (!stage) {
		return ERR_CANT_CREATE;
	}

	UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
	UsdGeomSetStageMetersPerUnit(stage, 1.0);

	for (int i = 0; i < p_generated_root->get_child_count(false); i++) {
		if (!export_composition_preserving_node(p_generated_root->get_child(i, false), stage, p_source_stage)) {
			return ERR_CANT_CREATE;
		}
	}

	if (p_generated_root->get_child_count(false) > 0) {
		Node *first_child = p_generated_root->get_child(0, false);
		const Dictionary first_metadata = get_usd_metadata(first_child);
		const String default_prim_path = first_metadata.get("usd:prim_path", String());
		if (!default_prim_path.is_empty()) {
			UsdPrim default_prim = stage->GetPrimAtPath(SdfPath(default_prim_path.utf8().get_data()));
			if (default_prim) {
				stage->SetDefaultPrim(default_prim);
			}
		}
	}

	const bool saved = stage->GetRootLayer()->Save();
	if (!saved || p_source_stage == nullptr || !p_source_stage->GetRootLayer()) {
		return saved ? OK : ERR_CANT_CREATE;
	}

	const String source_stage_path = to_godot_string(p_source_stage->GetRootLayer()->GetRealPath());
	const String target_base_dir = absolute_path.get_base_dir();
	const auto copy_relative_asset = [&](const String &p_asset_path) {
		if (p_asset_path.is_empty() || p_asset_path.is_absolute_path()) {
			return;
		}
		const String source_asset_path = get_absolute_path(source_stage_path.get_base_dir().path_join(p_asset_path));
		if (!FileAccess::file_exists(source_asset_path)) {
			return;
		}

		const String target_asset_path = get_absolute_path(target_base_dir.path_join(p_asset_path));
		const String target_asset_dir = target_asset_path.get_base_dir();
		if (!target_asset_dir.is_empty()) {
			const Error mkdir_error = DirAccess::make_dir_recursive_absolute(target_asset_dir);
			if (mkdir_error != OK) {
				return;
			}
		}

		Ref<FileAccess> source_file = FileAccess::open(source_asset_path, FileAccess::READ);
		if (source_file.is_null()) {
			return;
		}
		Ref<FileAccess> target_file = FileAccess::open(target_asset_path, FileAccess::WRITE);
		if (target_file.is_null()) {
			return;
		}
		target_file->store_buffer(source_file->get_buffer(source_file->get_length()));
	};

	std::function<void(Node *)> copy_relative_assets_recursive = [&](Node *p_node) {
		if (p_node == nullptr) {
			return;
		}
		const Dictionary metadata = get_usd_metadata(p_node);
		const Array references = metadata.get("usd:references", Array());
		for (int i = 0; i < references.size(); i++) {
			if (references[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			copy_relative_asset(((Dictionary)references[i]).get("asset_path", String()));
		}
		const Array payloads = metadata.get("usd:payloads", Array());
		for (int i = 0; i < payloads.size(); i++) {
			if (payloads[i].get_type() != Variant::DICTIONARY) {
				continue;
			}
			copy_relative_asset(((Dictionary)payloads[i]).get("asset_path", String()));
		}
		for (int i = 0; i < p_node->get_child_count(false); i++) {
			copy_relative_assets_recursive(p_node->get_child(i, false));
		}
	};
	copy_relative_assets_recursive(p_generated_root);

	return OK;
}

bool export_node_children_to_stage(Node *p_node, const SdfPath &p_parent_path, const UsdStageRefPtr &p_stage, const String &p_save_path, SdfPath *r_first_exported_path = nullptr);
void free_saver_root(Node *p_root);

void free_node_immediate(Node *p_node) {
	if (p_node == nullptr) {
		return;
	}
	p_node->call("free");
}

bool get_packed_stage_instance_data(const Ref<PackedScene> &p_packed_scene, Ref<UsdStageResource> *r_stage, Dictionary *r_variant_selections, Dictionary *r_runtime_node_overrides) {
	ERR_FAIL_COND_V(p_packed_scene.is_null(), false);
	ERR_FAIL_NULL_V(r_stage, false);
	ERR_FAIL_NULL_V(r_variant_selections, false);
	ERR_FAIL_NULL_V(r_runtime_node_overrides, false);

	Ref<SceneState> state = p_packed_scene->get_state();
	if (state.is_null() || state->get_node_count() <= 0) {
		return false;
	}
	if (String(state->get_node_type(0)) != "UsdStageInstance") {
		return false;
	}

	for (int i = 0; i < state->get_node_property_count(0); i++) {
		const StringName property_name = state->get_node_property_name(0, i);
		const Variant property_value = state->get_node_property_value(0, i);
		if (property_name == StringName("stage")) {
			*r_stage = property_value;
		} else if (property_name == StringName("variant_selections") && property_value.get_type() == Variant::DICTIONARY) {
			*r_variant_selections = property_value;
		} else if (property_name == StringName("runtime_node_overrides") && property_value.get_type() == Variant::DICTIONARY) {
			*r_runtime_node_overrides = property_value;
		}
	}

	return r_stage->is_valid();
}

void warn_packed_stage_instance_runtime_edits(const String &p_source_path, const Dictionary &p_runtime_node_overrides) {
	if (p_runtime_node_overrides.is_empty()) {
		return;
	}

	PackedStringArray edited_prims;
	const Array keys = p_runtime_node_overrides.keys();
	for (int i = 0; i < keys.size() && edited_prims.size() < 8; i++) {
		if (keys[i].get_type() == Variant::STRING || keys[i].get_type() == Variant::STRING_NAME) {
			edited_prims.push_back(String(keys[i]));
		}
	}

	UtilityFunctions::push_warning(vformat(
			"USD source-preserving save detected generated edits below a composition boundary in %s. These edits cannot be merged into preserved USD composition and will not be written by the source-preserving path. Edited prims: %s",
			p_source_path,
			String(", ").join(edited_prims)));
}

struct UsdMeshSurfaceFaceRange {
	int face_start = 0;
	int face_count = 0;
};

bool write_mesh_geometry(const Ref<Mesh> &p_mesh, UsdGeomMesh p_usd_mesh, std::vector<UsdMeshSurfaceFaceRange> *r_surface_face_ranges = nullptr) {
	ERR_FAIL_COND_V(p_mesh.is_null(), false);
	if (!p_usd_mesh) {
		return false;
	}
	Ref<ArrayMesh> array_mesh = p_mesh;
	ERR_FAIL_COND_V(array_mesh.is_null(), false);

	VtArray<GfVec3f> points;
	VtArray<int> face_vertex_counts;
	VtArray<int> face_vertex_indices;
	VtArray<GfVec3f> normals;
	VtArray<GfVec2f> uvs;
	VtArray<GfVec3f> display_colors;
	VtArray<float> display_opacities;
	bool have_normals = true;
	bool have_uvs = true;
	bool have_colors = true;
	bool have_opacities = true;

	if (r_surface_face_ranges != nullptr) {
		r_surface_face_ranges->clear();
		r_surface_face_ranges->resize(p_mesh->get_surface_count());
	}

	for (int surface_index = 0; surface_index < p_mesh->get_surface_count(); surface_index++) {
		const Array arrays = p_mesh->surface_get_arrays(surface_index);
		if (arrays.size() != Mesh::ARRAY_MAX) {
			continue;
		}

		const PackedVector3Array vertices = arrays[Mesh::ARRAY_VERTEX];
		if (vertices.is_empty()) {
			continue;
		}
		if (array_mesh->surface_get_primitive_type(surface_index) != Mesh::PRIMITIVE_TRIANGLES) {
			return false;
		}

		const PackedVector3Array surface_normals = arrays[Mesh::ARRAY_NORMAL];
		const PackedVector2Array surface_uvs = arrays[Mesh::ARRAY_TEX_UV];
		const PackedColorArray surface_colors = arrays[Mesh::ARRAY_COLOR];
		const PackedInt32Array indices = arrays[Mesh::ARRAY_INDEX];
		const int vertex_offset = (int)points.size();
		const int face_start = (int)face_vertex_counts.size();

		for (int i = 0; i < vertices.size(); i++) {
			const Vector3 vertex = vertices[i];
			points.push_back(GfVec3f(vertex.x, vertex.y, vertex.z));
		}

		if (have_normals && surface_normals.size() == vertices.size()) {
			for (int i = 0; i < surface_normals.size(); i++) {
				const Vector3 normal = surface_normals[i];
				normals.push_back(GfVec3f(normal.x, normal.y, normal.z));
			}
		} else {
			have_normals = false;
		}

		if (have_uvs && surface_uvs.size() == vertices.size()) {
			for (int i = 0; i < surface_uvs.size(); i++) {
				const Vector2 uv = surface_uvs[i];
				uvs.push_back(GfVec2f(uv.x, uv.y));
			}
		} else {
			have_uvs = false;
		}

		if (have_colors && surface_colors.size() == vertices.size()) {
			for (int i = 0; i < surface_colors.size(); i++) {
				const Color color = surface_colors[i];
				display_colors.push_back(GfVec3f(color.r, color.g, color.b));
				display_opacities.push_back(color.a);
			}
		} else {
			have_colors = false;
			have_opacities = false;
		}

		int emitted_faces = 0;
		if (!indices.is_empty()) {
			for (int i = 0; i + 2 < indices.size(); i += 3) {
				face_vertex_counts.push_back(3);
				face_vertex_indices.push_back(vertex_offset + indices[i]);
				face_vertex_indices.push_back(vertex_offset + indices[i + 2]);
				face_vertex_indices.push_back(vertex_offset + indices[i + 1]);
				emitted_faces++;
			}
		} else {
			for (int i = 0; i + 2 < vertices.size(); i += 3) {
				face_vertex_counts.push_back(3);
				face_vertex_indices.push_back(vertex_offset + i);
				face_vertex_indices.push_back(vertex_offset + i + 2);
				face_vertex_indices.push_back(vertex_offset + i + 1);
				emitted_faces++;
			}
		}

		if (r_surface_face_ranges != nullptr) {
			(*r_surface_face_ranges)[surface_index].face_start = face_start;
			(*r_surface_face_ranges)[surface_index].face_count = emitted_faces;
		}
	}

	if (points.empty() || face_vertex_counts.empty()) {
		return false;
	}

	p_usd_mesh.CreatePointsAttr().Set(points);
	p_usd_mesh.CreateFaceVertexCountsAttr().Set(face_vertex_counts);
	p_usd_mesh.CreateFaceVertexIndicesAttr().Set(face_vertex_indices);
	p_usd_mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
	p_usd_mesh.CreateOrientationAttr().Set(UsdGeomTokens->rightHanded);

	if (have_normals && normals.size() == points.size()) {
		p_usd_mesh.CreateNormalsAttr().Set(normals);
		p_usd_mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
	}

	if (have_uvs && uvs.size() == points.size()) {
		UsdGeomPrimvarsAPI primvars_api(p_usd_mesh);
		UsdGeomPrimvar st = primvars_api.CreatePrimvar(TfToken("st"), SdfValueTypeNames->TexCoord2fArray, UsdGeomTokens->vertex);
		st.Set(uvs);
	}

	if (have_colors && display_colors.size() == points.size()) {
		UsdGeomGprim gprim(p_usd_mesh);
		gprim.CreateDisplayColorPrimvar(UsdGeomTokens->vertex).Set(display_colors);
		if (have_opacities && display_opacities.size() == points.size()) {
			gprim.CreateDisplayOpacityPrimvar(UsdGeomTokens->vertex).Set(display_opacities);
		}
	}

	return true;
}

void write_mesh_material_binding(const UsdStageRefPtr &p_stage, MeshInstance3D *p_mesh_instance, const UsdGeomMesh &p_usd_mesh, const SdfPath &p_mesh_path, const String &p_save_path, const std::vector<UsdMeshSurfaceFaceRange> &p_surface_face_ranges) {
	ERR_FAIL_NULL(p_mesh_instance);
	if (!p_usd_mesh || p_stage == nullptr) {
		return;
	}

	Ref<Mesh> mesh = p_mesh_instance->get_mesh();
	if (mesh.is_null() || mesh->get_surface_count() == 0 || p_surface_face_ranges.size() != (size_t)mesh->get_surface_count()) {
		return;
	}

	const Dictionary usd_metadata = get_usd_metadata(p_mesh_instance);
	const Array surface_descriptions = usd_metadata.get("usd:material_subsets", Array());
	const bool has_surface_descriptions = surface_descriptions.size() == mesh->get_surface_count();
	const auto get_surface_description = [&](int p_surface_index) -> Dictionary {
		if (!has_surface_descriptions || surface_descriptions[p_surface_index].get_type() != Variant::DICTIONARY) {
			return Dictionary();
		}
		return surface_descriptions[p_surface_index];
	};
	const auto make_subset_faces = [&](const UsdMeshSurfaceFaceRange &p_surface_range) -> VtIntArray {
		VtIntArray subset_faces;
		subset_faces.reserve(p_surface_range.face_count);
		for (int face_index = 0; face_index < p_surface_range.face_count; face_index++) {
			subset_faces.push_back(p_surface_range.face_start + face_index);
		}
		return subset_faces;
	};

	std::unordered_map<uint64_t, UsdShadeMaterial> material_cache;
	std::unordered_map<std::string, int> material_name_counts;
	const auto resolve_usd_material = [&](const Ref<Material> &p_material) -> UsdShadeMaterial {
		if (p_material.is_null()) {
			return UsdShadeMaterial();
		}

		const uint64_t material_id = p_material->get_instance_id();
		if (material_cache.count(material_id) != 0) {
			return material_cache[material_id];
		}

		String base_name = String(p_material->get_name());
		if (base_name.is_empty()) {
			base_name = "Material";
		}
		base_name = make_valid_prim_identifier(base_name);
		const std::string base_key = base_name.utf8().get_data();
		const int name_count = material_name_counts[base_key];
		material_name_counts[base_key] = name_count + 1;
		const String material_key = name_count == 0 ? base_name : vformat("%s_%d", base_name, name_count + 1);

		UsdShadeMaterial usd_material;
		if (write_preview_material(p_stage, p_material, p_mesh_path, p_save_path, material_key, &usd_material) && usd_material) {
			material_cache[material_id] = usd_material;
			return usd_material;
		}
		return UsdShadeMaterial();
	};

	bool preserve_subset_structure = false;
	if (has_surface_descriptions) {
		for (int surface_index = 0; surface_index < surface_descriptions.size(); surface_index++) {
			const Dictionary description = get_surface_description(surface_index);
			if ((String)description.get("binding_kind", String()) == String("subset")) {
				preserve_subset_structure = true;
				break;
			}
		}
	}

	if (preserve_subset_structure) {
		UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim());
		for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
			const UsdMeshSurfaceFaceRange &surface_range = p_surface_face_ranges[surface_index];
			if (surface_range.face_count <= 0) {
				continue;
			}

			const Dictionary description = get_surface_description(surface_index);
			const String binding_kind = description.get("binding_kind", String("mesh"));
			const bool has_material_binding = description.get("has_material_binding", true);

			if (binding_kind != "subset") {
				if (!has_material_binding) {
					continue;
				}

				const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
				if (surface_material.is_null()) {
					continue;
				}

				const UsdShadeMaterial usd_material = resolve_usd_material(surface_material);
				if (usd_material) {
					UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim()).Bind(usd_material);
				}
				continue;
			}

			const String subset_path_string = description.get("subset_path", String());
			const String subset_name = description.get("subset_name", vformat("Surface_%d", surface_index));
			const String family_name_string = description.get("family_name", String("materialBind"));
			const String family_type_string = description.get("family_type", String("nonOverlapping"));
			const TfToken family_name = TfToken(family_name_string.utf8().get_data());
			const TfToken family_type = TfToken(family_type_string.utf8().get_data());
			UsdGeomSubset subset = define_preserved_subset(p_usd_mesh, subset_path_string, subset_name, UsdGeomTokens->face, make_subset_faces(surface_range), family_name, family_type);
			if (!subset || !has_material_binding) {
				continue;
			}

			const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
			if (surface_material.is_null()) {
				continue;
			}

			const UsdShadeMaterial usd_material = resolve_usd_material(surface_material);
			if (usd_material) {
				UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(usd_material);
			}
		}
		return;
	}

	Ref<Material> shared_material;
	bool can_use_shared_binding = true;
	for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
		if (p_surface_face_ranges[surface_index].face_count <= 0) {
			continue;
		}
		const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
		if (surface_material.is_null()) {
			can_use_shared_binding = false;
			continue;
		}
		if (shared_material.is_null()) {
			shared_material = surface_material;
		} else if (shared_material != surface_material) {
			can_use_shared_binding = false;
		}
	}

	if (can_use_shared_binding && shared_material.is_valid()) {
		const UsdShadeMaterial usd_material = resolve_usd_material(shared_material);
		if (usd_material) {
			UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim()).Bind(usd_material);
		}
		return;
	}

	UsdShadeMaterialBindingAPI::Apply(p_usd_mesh.GetPrim());
	for (int surface_index = 0; surface_index < mesh->get_surface_count(); surface_index++) {
		const UsdMeshSurfaceFaceRange &surface_range = p_surface_face_ranges[surface_index];
		if (surface_range.face_count <= 0) {
			continue;
		}

		const Ref<Material> surface_material = p_mesh_instance->get_active_material(surface_index);
		if (surface_material.is_null()) {
			continue;
		}

		VtIntArray subset_faces;
		subset_faces = make_subset_faces(surface_range);

		const String subset_name = vformat("Surface_%d", surface_index);
		UsdGeomSubset subset = UsdGeomSubset::CreateGeomSubset(p_usd_mesh, TfToken(subset_name.utf8().get_data()), UsdGeomTokens->face, subset_faces, UsdShadeTokens->materialBind, UsdGeomTokens->nonOverlapping);
		const UsdShadeMaterial usd_material = resolve_usd_material(surface_material);
		if (usd_material) {
			UsdShadeMaterialBindingAPI::Apply(subset.GetPrim()).Bind(usd_material);
		}
	}
}

void bind_mesh_instance_material(MeshInstance3D *p_mesh_instance, const UsdPrim &p_prim, const SdfPath &p_prim_path, const UsdStageRefPtr &p_stage, const String &p_save_path) {
	ERR_FAIL_NULL(p_mesh_instance);
	ERR_FAIL_COND(p_stage == nullptr);
	if (!p_prim) {
		return;
	}

	Ref<Material> material = p_mesh_instance->get_material_override();
	if (material.is_null()) {
		Ref<Mesh> mesh = p_mesh_instance->get_mesh();
		if (mesh.is_valid() && mesh->get_surface_count() > 0) {
			material = p_mesh_instance->get_active_material(0);
		}
	}
	if (material.is_null()) {
		return;
	}

	UsdShadeMaterial usd_material;
	if (write_preview_material(p_stage, material, p_prim_path, p_save_path, material->get_name(), &usd_material) && usd_material) {
		UsdShadeMaterialBindingAPI::Apply(p_prim).Bind(usd_material);
	}
}

bool export_mesh_instance_to_stage(MeshInstance3D *p_mesh_instance, const SdfPath &p_prim_path, const UsdStageRefPtr &p_stage, const String &p_save_path) {
	ERR_FAIL_NULL_V(p_mesh_instance, false);
	ERR_FAIL_COND_V(p_stage == nullptr, false);

	Ref<Mesh> mesh = p_mesh_instance->get_mesh();
	if (mesh.is_null()) {
		UsdGeomXform xform = UsdGeomXform::Define(p_stage, p_prim_path);
		apply_node3d_transform(p_mesh_instance, xform);
		export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
		return true;
	}

	Ref<ArrayMesh> array_mesh = mesh;
	if (array_mesh.is_valid()) {
		UsdGeomMesh usd_mesh = UsdGeomMesh::Define(p_stage, p_prim_path);
		std::vector<UsdMeshSurfaceFaceRange> surface_face_ranges;
		if (write_mesh_geometry(array_mesh, usd_mesh, &surface_face_ranges)) {
			apply_node3d_transform(p_mesh_instance, usd_mesh);
			write_mesh_material_binding(p_stage, p_mesh_instance, usd_mesh, p_prim_path, p_save_path, surface_face_ranges);
			export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
			return true;
		}
	}

	Ref<BoxMesh> box_mesh = mesh;
	if (box_mesh.is_valid()) {
		const Vector3 size = box_mesh->get_size();
		UsdGeomCube cube = UsdGeomCube::Define(p_stage, p_prim_path);
		cube.GetSizeAttr().Set(1.0);
		const Transform3D transform = p_mesh_instance->get_transform();
		const Vector3 combined_scale = transform.basis.get_scale() * size;
		apply_transform_components(transform.origin, transform.basis.get_rotation_quaternion(), combined_scale, p_mesh_instance->is_visible(), cube);
		bind_mesh_instance_material(p_mesh_instance, cube.GetPrim(), p_prim_path, p_stage, p_save_path);
		export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
		return true;
	}

	Ref<SphereMesh> sphere_mesh = mesh;
	if (sphere_mesh.is_valid()) {
		UsdGeomSphere sphere = UsdGeomSphere::Define(p_stage, p_prim_path);
		sphere.GetRadiusAttr().Set((double)sphere_mesh->get_radius());
		apply_node3d_transform(p_mesh_instance, sphere);
		bind_mesh_instance_material(p_mesh_instance, sphere.GetPrim(), p_prim_path, p_stage, p_save_path);
		export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
		return true;
	}

	Ref<CapsuleMesh> capsule_mesh = mesh;
	if (capsule_mesh.is_valid()) {
		UsdGeomCapsule capsule = UsdGeomCapsule::Define(p_stage, p_prim_path);
		capsule.GetRadiusAttr().Set((double)capsule_mesh->get_radius());
		capsule.GetHeightAttr().Set((double)capsule_mesh->get_height());
		capsule.GetAxisAttr().Set(UsdGeomTokens->y);
		apply_node3d_transform(p_mesh_instance, capsule);
		bind_mesh_instance_material(p_mesh_instance, capsule.GetPrim(), p_prim_path, p_stage, p_save_path);
		export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
		return true;
	}

	Ref<CylinderMesh> cylinder_mesh = mesh;
	if (cylinder_mesh.is_valid()) {
		const float top_radius = cylinder_mesh->get_top_radius();
		const float bottom_radius = cylinder_mesh->get_bottom_radius();
		if (Math::is_equal_approx(top_radius, 0.0f)) {
			UsdGeomCone cone = UsdGeomCone::Define(p_stage, p_prim_path);
			cone.GetRadiusAttr().Set((double)bottom_radius);
			cone.GetHeightAttr().Set((double)cylinder_mesh->get_height());
			cone.GetAxisAttr().Set(UsdGeomTokens->y);
			apply_node3d_transform(p_mesh_instance, cone);
			bind_mesh_instance_material(p_mesh_instance, cone.GetPrim(), p_prim_path, p_stage, p_save_path);
			export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
			return true;
		}
		if (Math::is_equal_approx(top_radius, bottom_radius)) {
			UsdGeomCylinder cylinder = UsdGeomCylinder::Define(p_stage, p_prim_path);
			cylinder.GetRadiusAttr().Set((double)top_radius);
			cylinder.GetHeightAttr().Set((double)cylinder_mesh->get_height());
			cylinder.GetAxisAttr().Set(UsdGeomTokens->y);
			apply_node3d_transform(p_mesh_instance, cylinder);
			bind_mesh_instance_material(p_mesh_instance, cylinder.GetPrim(), p_prim_path, p_stage, p_save_path);
			export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
			return true;
		}
	}

	Ref<PlaneMesh> plane_mesh = mesh;
	if (plane_mesh.is_valid()) {
		const Vector2 size = plane_mesh->get_size();
		UsdGeomPlane plane = UsdGeomPlane::Define(p_stage, p_prim_path);
		plane.GetWidthAttr().Set((double)size.x);
		plane.GetLengthAttr().Set((double)size.y);
		plane.GetAxisAttr().Set(UsdGeomTokens->y);
		apply_node3d_transform(p_mesh_instance, plane);
		bind_mesh_instance_material(p_mesh_instance, plane.GetPrim(), p_prim_path, p_stage, p_save_path);
		export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
		return true;
	}

	UsdGeomXform fallback_xform = UsdGeomXform::Define(p_stage, p_prim_path);
	apply_node3d_transform(p_mesh_instance, fallback_xform);
	set_usd_metadata(p_mesh_instance, "usd:save_status", "Unsupported Mesh resource type for baseline saver; exported transform hierarchy only.");
	export_node_children_to_stage(p_mesh_instance, p_prim_path, p_stage, p_save_path);
	return true;
}

bool export_node_to_stage(Node *p_node, const SdfPath &p_prim_path, const UsdStageRefPtr &p_stage, const String &p_save_path) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(p_stage == nullptr, false);

	if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(p_node)) {
		return export_mesh_instance_to_stage(mesh_instance, p_prim_path, p_stage, p_save_path);
	}

	if (Node3D *node_3d = Object::cast_to<Node3D>(p_node)) {
		UsdGeomXform xform = UsdGeomXform::Define(p_stage, p_prim_path);
		apply_node3d_transform(node_3d, xform);
		export_node_children_to_stage(p_node, p_prim_path, p_stage, p_save_path);
		return true;
	}

	return false;
}

bool export_node_children_to_stage(Node *p_node, const SdfPath &p_parent_path, const UsdStageRefPtr &p_stage, const String &p_save_path, SdfPath *r_first_exported_path) {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_COND_V(p_stage == nullptr, false);

	std::unordered_map<std::string, int> name_counts;
	bool exported_any_child = false;
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		Node *child = p_node->get_child(i, false);
		const String base_name = make_valid_prim_identifier(child->get_name());
		const std::string base_key = base_name.utf8().get_data();
		int &name_count = name_counts[base_key];
		const String prim_name = name_count == 0 ? base_name : vformat("%s_%d", base_name, name_count);
		name_count++;

		const SdfPath child_path = p_parent_path.AppendChild(make_valid_prim_token(prim_name));
		if (export_node_to_stage(child, child_path, p_stage, p_save_path)) {
			if (r_first_exported_path != nullptr && r_first_exported_path->IsEmpty()) {
				*r_first_exported_path = child_path;
			}
			exported_any_child = true;
		}
	}
	return exported_any_child;
}

Error save_node_tree_to_stage(Node *root, const String &p_path) {
	ERR_FAIL_NULL_V(root, ERR_INVALID_PARAMETER);
	const String absolute_path = get_absolute_path(p_path);
	const String extension = p_path.get_extension().to_lower();
	const bool package_as_usdz = extension == "usdz";
	String stage_save_path = absolute_path;
	String package_directory;
	String package_root_layer_name;
	if (package_as_usdz) {
		package_directory = get_absolute_path("user://godot_usdz_composed_save");
		const Error mkdir_error = DirAccess::make_dir_recursive_absolute(package_directory);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
		package_root_layer_name = p_path.get_file().get_basename() + ".usda";
		stage_save_path = package_directory.path_join(package_root_layer_name);
	}

	Node *export_root = root;
	bool export_generated_children_only = false;
	if (UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(root)) {
		stage_instance->rebuild();
		for (int i = 0; i < root->get_child_count(false); i++) {
			Node *child = root->get_child(i, false);
			if (child->has_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META))) {
				export_root = child;
				export_generated_children_only = true;
				break;
			}
		}
	}

	const String base_dir = stage_save_path.get_base_dir();
	if (!base_dir.is_empty()) {
		const Error mkdir_error = DirAccess::make_dir_recursive_absolute(base_dir);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
	}

	UsdStageRefPtr stage = UsdStage::CreateNew(stage_save_path.utf8().get_data());
	if (!stage) {
		return ERR_CANT_CREATE;
	}

	UsdGeomSetStageUpAxis(stage, UsdGeomTokens->y);
	UsdGeomSetStageMetersPerUnit(stage, 1.0);

	UsdPrim default_prim;
	if (export_generated_children_only) {
		SdfPath first_exported_path;
		if (!export_node_children_to_stage(export_root, SdfPath::AbsoluteRootPath(), stage, stage_save_path, &first_exported_path)) {
			return ERR_CANT_CREATE;
		}
		if (!first_exported_path.IsEmpty()) {
			default_prim = stage->GetPrimAtPath(first_exported_path);
		}
	} else {
		String root_name_source = String(export_root->get_name());
		if (root_name_source.is_empty()) {
			root_name_source = "Root";
		}
		const String root_name = make_valid_prim_identifier(root_name_source);
		const SdfPath root_path("/" + std::string(root_name.utf8().get_data()));
		if (!export_node_to_stage(export_root, root_path, stage, stage_save_path)) {
			UsdGeomXform root_xform = UsdGeomXform::Define(stage, root_path);
			if (Node3D *root_node_3d = Object::cast_to<Node3D>(export_root)) {
				apply_node3d_transform(root_node_3d, root_xform);
			}
			if (!export_node_children_to_stage(export_root, root_path, stage, stage_save_path)) {
				return ERR_CANT_CREATE;
			}
		}
		default_prim = stage->GetPrimAtPath(root_path);
	}

	if (default_prim) {
		stage->SetDefaultPrim(default_prim);
	}

	const bool saved = stage->GetRootLayer()->Save();
	if (!saved) {
		return ERR_CANT_CREATE;
	}

	if (package_as_usdz) {
		Vector<String> package_file_paths;
		package_file_paths.push_back(package_root_layer_name);
		return create_usdz_package_from_extracted_files(package_directory, package_file_paths, package_root_layer_name, absolute_path);
	}

	return OK;
}

Error save_authored_packed_scene_to_stage(const Ref<PackedScene> &p_packed_scene, const String &p_path) {
	ERR_FAIL_COND_V(p_packed_scene.is_null(), ERR_INVALID_PARAMETER);

	Node *root = p_packed_scene->instantiate();
	ERR_FAIL_NULL_V(root, ERR_CANT_CREATE);
	const Error save_error = save_node_tree_to_stage(root, p_path);
	free_saver_root(root);
	return save_error;
}

Error save_composed_usd_stage_to_path(const String &p_source_path, const Dictionary &p_variant_selections, const String &p_path) {
	UsdStageRefPtr composed_stage = open_stage_for_instance(p_source_path, p_variant_selections);
	if (!composed_stage) {
		return ERR_CANT_OPEN;
	}

	const String absolute_path = get_absolute_path(p_path);
	const String extension = p_path.get_extension().to_lower();
	String export_path = absolute_path;
	String package_directory;
	String package_root_layer_name;
	if (extension == "usdz") {
		package_directory = get_absolute_path("user://godot_usdz_composed_save");
		const Error mkdir_error = DirAccess::make_dir_recursive_absolute(package_directory);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
		package_root_layer_name = p_path.get_file().get_basename() + ".usda";
		export_path = package_directory.path_join(package_root_layer_name);
	}

	const String base_dir = export_path.get_base_dir();
	if (!base_dir.is_empty()) {
		const Error mkdir_error = DirAccess::make_dir_recursive_absolute(base_dir);
		if (mkdir_error != OK) {
			return mkdir_error;
		}
	}

	if (!composed_stage->Export(export_path.utf8().get_data())) {
		return ERR_CANT_CREATE;
	}

	if (extension == "usdz") {
		Vector<String> package_file_paths;
		package_file_paths.push_back(package_root_layer_name);
		return create_usdz_package_from_extracted_files(package_directory, package_file_paths, package_root_layer_name, absolute_path);
	}

	return OK;
}

void free_saver_root(Node *p_root) {
	if (p_root == nullptr) {
		return;
	}
	if (UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(p_root)) {
		stage_instance->set_stage(Ref<UsdStageResource>());
	}
	p_root->call("free");
}

class UsdSceneBuilder {
	const UsdStageRefPtr stage;
	const UsdTimeCode time = UsdTimeCode::Default();
	const double meters_per_unit = 1.0;
	const TfToken up_axis;
	const Dictionary variant_catalog;

	Transform3D get_stage_correction_transform() const {
		Basis root_basis;
		if (up_axis == UsdGeomTokens->z) {
			root_basis = Basis(Vector3(1, 0, 0), (real_t)-Math_PI * 0.5);
		}
		root_basis = root_basis.scaled(Vector3((real_t)meters_per_unit, (real_t)meters_per_unit, (real_t)meters_per_unit));
		return Transform3D(root_basis, Vector3());
	}

	Dictionary get_local_variant_sets(const String &p_prim_path) const {
		const Variant local_variant_sets = variant_catalog.get(p_prim_path, Variant());
		if (local_variant_sets.get_type() == Variant::DICTIONARY) {
			return local_variant_sets;
		}
		return Dictionary();
	}

	Dictionary make_common_metadata(const UsdPrim &p_prim) const {
		Dictionary metadata;
		const String prim_path = to_godot_string(p_prim.GetPath().GetString());
		metadata["usd:prim_path"] = prim_path;
		metadata["usd:type_name"] = to_godot_string(p_prim.GetTypeName().GetString());
		metadata["usd:active"] = p_prim.IsActive();

		const Dictionary local_variant_sets = get_local_variant_sets(prim_path);
		if (!local_variant_sets.is_empty()) {
			metadata["usd:variant_boundary"] = true;
			metadata["usd:variant_sets"] = local_variant_sets;
		}

		const Array variant_context = collect_variant_context(variant_catalog, prim_path);
		if (!variant_context.is_empty()) {
			metadata["usd:variant_context"] = variant_context;
		}

		Array applied_schemas;
		for (const TfToken &schema : p_prim.GetAppliedSchemas()) {
			applied_schemas.push_back(to_godot_string(schema.GetString()));
		}
		metadata["usd:applied_schemas"] = applied_schemas;
		return metadata;
	}

	void apply_transform_and_visibility(const UsdPrim &p_prim, Node *p_node) const {
		Node3D *node_3d = Object::cast_to<Node3D>(p_node);
		if (node_3d == nullptr) {
			return;
		}

		UsdGeomXformable xformable(p_prim);
		if (xformable) {
			GfMatrix4d local_matrix(1.0);
			bool resets_xform_stack = false;
			const bool has_local_transform = xformable.GetLocalTransformation(&local_matrix, &resets_xform_stack, time);
			if (resets_xform_stack) {
				Transform3D corrected_world_transform;
				bool has_corrected_world_transform = false;
				UsdGeomImageable imageable(p_prim);
				if (imageable) {
					corrected_world_transform = get_stage_correction_transform() * gf_matrix_to_transform(imageable.ComputeLocalToWorldTransform(time));
					has_corrected_world_transform = true;
				} else if (has_local_transform) {
					corrected_world_transform = get_stage_correction_transform() * gf_matrix_to_transform(local_matrix);
					has_corrected_world_transform = true;
				}

				node_3d->set_as_top_level(true);
				if (has_corrected_world_transform) {
					node_3d->set_global_transform(corrected_world_transform);
				}
				set_usd_metadata(node_3d, "usd:resets_xform_stack", true);
			} else if (has_local_transform) {
				node_3d->set_transform(gf_matrix_to_transform(local_matrix));
			}
		}

		UsdGeomImageable imageable(p_prim);
		if (imageable) {
			const TfToken visibility = imageable.ComputeVisibility(time);
			if (visibility == UsdGeomTokens->invisible) {
				node_3d->set_visible(false);
			}
		}
	}

		bool should_skip_child_prim(const UsdPrim &p_prim) const {
			if (p_prim.IsA<UsdGeomSubset>() || p_prim.IsA<UsdShadeMaterial>() || p_prim.IsA<UsdShadeShader>() || p_prim.IsA<UsdSkelAnimation>() || p_prim.IsA<UsdSkelBlendShape>()) {
				return true;
			}
			return false;
		}

	bool stage_has_authored_lights() const {
		for (const UsdPrim &prim : stage->Traverse()) {
			if (prim.HasAPI<UsdLuxLightAPI>()) {
				return true;
			}
		}
		return false;
	}

	void append_preview_lighting(Node3D *p_root, const String &p_reason) const {
		ERR_FAIL_NULL(p_root);

		Dictionary preview_metadata;
		preview_metadata["usd:generated_preview"] = true;
		preview_metadata["usd:preview_only"] = true;
		preview_metadata["usd:generated_preview_reason"] = p_reason;

		Ref<Environment> preview_environment;
		preview_environment.instantiate();
		preview_environment->set_background(Environment::BG_COLOR);
		preview_environment->set_bg_color(Color(0.09f, 0.10f, 0.12f));
		preview_environment->set_ambient_source(Environment::AMBIENT_SOURCE_COLOR);
		preview_environment->set_ambient_light_color(Color(0.42f, 0.44f, 0.48f));
		preview_environment->set_ambient_light_energy(0.7f);
		preview_environment->set_ambient_light_sky_contribution(0.0f);

		WorldEnvironment *world_environment = memnew(WorldEnvironment);
		world_environment->set_name("USDPreviewEnvironment");
		world_environment->set_environment(preview_environment);
		set_usd_metadata_entries(world_environment, preview_metadata);
		set_usd_metadata(world_environment, "usd:generated_preview_kind", "world_environment");
		p_root->add_child(world_environment);

		DirectionalLight3D *preview_sun = memnew(DirectionalLight3D);
		preview_sun->set_name("USDPreviewSun");
		preview_sun->set_rotation_degrees(Vector3(-50.0f, 30.0f, 0.0f));
		preview_sun->set_color(Color(1.0f, 0.97f, 0.92f));
		preview_sun->set_param(Light3D::PARAM_INTENSITY, 40000.0f);
		preview_sun->set_shadow(true);
		set_usd_metadata_entries(preview_sun, preview_metadata);
		set_usd_metadata(preview_sun, "usd:generated_preview_kind", "directional_light");
		p_root->add_child(preview_sun);
	}

	template <typename TLight>
	void apply_light_attributes(const TLight &p_light, Light3D *p_target) const {
		ERR_FAIL_NULL(p_target);

		GfVec3f color_value(1.0f);
		float intensity = 1.0f;
		float exposure = 0.0f;

		p_light.GetColorAttr().Get(&color_value, time);
		p_light.GetIntensityAttr().Get(&intensity, time);
		p_light.GetExposureAttr().Get(&exposure, time);

		p_target->set_color(Color(color_value[0], color_value[1], color_value[2], 1.0f));
		p_target->set_param(Light3D::PARAM_INTENSITY, intensity * Math::pow(2.0f, exposure));
	}

	bool apply_shaping_to_spot_light(const UsdPrim &p_prim, SpotLight3D *p_target) const {
		ERR_FAIL_NULL_V(p_target, false);
		if (!p_prim.HasAPI<UsdLuxShapingAPI>()) {
			return false;
		}

		UsdLuxShapingAPI shaping_api(p_prim);
		if (!shaping_api) {
			return false;
		}

		float cone_angle = 90.0f;
		shaping_api.GetShapingConeAngleAttr().Get(&cone_angle, time);
		p_target->set_param(Light3D::PARAM_SPOT_ANGLE, CLAMP(cone_angle, 0.1f, 90.0f));

		float cone_softness = 0.0f;
		shaping_api.GetShapingConeSoftnessAttr().Get(&cone_softness, time);
		p_target->set_param(Light3D::PARAM_SPOT_ATTENUATION, MAX(0.01f, 1.0f - CLAMP(cone_softness, 0.0f, 1.0f)));
		return true;
	}

	Node *build_area_light3d_dynamic(const Vector2 &p_area_size, const Ref<Texture2D> &p_area_texture, float p_range, const Color &p_color, float p_intensity, const Dictionary &p_mapping_notes = Dictionary()) const {
		ClassDBSingleton *class_db = ClassDBSingleton::get_singleton();
		if (class_db == nullptr || !class_db->class_exists(StringName("AreaLight3D")) || !class_db->can_instantiate(StringName("AreaLight3D"))) {
			return nullptr;
		}

		Object *area_object = class_db->instantiate(StringName("AreaLight3D"));
		if (area_object == nullptr) {
			return nullptr;
		}

		Node *node = Object::cast_to<Node>(area_object);
		Light3D *light = Object::cast_to<Light3D>(area_object);
		if (node == nullptr || light == nullptr) {
			memdelete(area_object);
			return nullptr;
		}

		light->set_color(p_color);
		light->set_param(Light3D::PARAM_INTENSITY, p_intensity);
		light->set_param(Light3D::PARAM_RANGE, p_range);
		area_object->call(StringName("set_area_size"), p_area_size);
		if (p_area_texture.is_valid()) {
			area_object->call(StringName("set_area_texture"), p_area_texture);
		}

		Array mapping_keys = p_mapping_notes.keys();
		for (int i = 0; i < mapping_keys.size(); i++) {
			const Variant key = mapping_keys[i];
			if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
				set_usd_metadata(area_object, String(key), p_mapping_notes[key]);
			}
		}

		return node;
	}

	UsdAreaLightProxy *build_area_light_proxy(const String &p_source_schema, const String &p_shape, const Vector2 &p_area_size, const Ref<Texture2D> &p_area_texture, float p_range, const Color &p_color, float p_intensity, const Dictionary &p_mapping_notes = Dictionary()) const {
		UsdAreaLightProxy *proxy = memnew(UsdAreaLightProxy);
		proxy->set_source_schema(p_source_schema);
		proxy->set_light_shape(p_shape);
		proxy->set_area_size(p_area_size);
		proxy->set_area_texture(p_area_texture);
		proxy->set_light_color(p_color);
		proxy->set_light_intensity(p_intensity);
		proxy->set_light_range(p_range);

		Array mapping_keys = p_mapping_notes.keys();
		for (int i = 0; i < mapping_keys.size(); i++) {
			const Variant key = mapping_keys[i];
			if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
				set_usd_metadata(proxy, String(key), p_mapping_notes[key]);
			}
		}

		return proxy;
	}

	Node *build_node(const UsdPrim &p_prim) const {
		Node *node = nullptr;

		if (p_prim.IsA<UsdGeomMesh>()) {
			Dictionary mapping_notes;
			MeshBuildResult mesh_result = build_polygon_mesh(stage, time, UsdGeomMesh(p_prim), &mapping_notes);
			MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
			if (mesh_result.mesh.is_valid()) {
				mesh_instance->set_mesh(mesh_result.mesh);
			}
			if (!mesh_result.material_paths.is_empty()) {
				set_usd_metadata(mesh_instance, "usd:material_bindings", mesh_result.material_paths);
			}
			if (!mesh_result.material_subsets.is_empty()) {
				set_usd_metadata(mesh_instance, "usd:material_subsets", mesh_result.material_subsets);
			}
			Array mapping_note_keys = mapping_notes.keys();
			for (int i = 0; i < mapping_note_keys.size(); i++) {
				const Variant key = mapping_note_keys[i];
				if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
					set_usd_metadata(mesh_instance, String(key), mapping_notes[key]);
				}
			}
			node = mesh_instance;
		} else if (p_prim.IsA<UsdGeomPoints>()) {
			Dictionary mapping_notes;
			Node *points_node = build_points_instance(stage, time, p_prim, &mapping_notes);
			if (points_node == nullptr) {
				return nullptr;
			}
			Array mapping_note_keys = mapping_notes.keys();
			for (int i = 0; i < mapping_note_keys.size(); i++) {
				const Variant key = mapping_note_keys[i];
				if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
					set_usd_metadata(points_node, String(key), mapping_notes[key]);
				}
			}
			node = points_node;
		} else if (Node *primitive = build_primitive_mesh_instance(time, p_prim)) {
			if (MeshInstance3D *mesh_instance = Object::cast_to<MeshInstance3D>(primitive)) {
				Dictionary mapping_notes;
				UsdShadeMaterial bound_material = UsdShadeMaterialBindingAPI(p_prim).ComputeBoundMaterial();
				if (bound_material) {
					Ref<Material> material = build_material_from_usd_material(stage, time, bound_material, &mapping_notes);
					if (material.is_valid()) {
						mesh_instance->set_material_override(material);
						Array material_bindings;
						material_bindings.push_back(to_godot_string(bound_material.GetPath().GetString()));
						set_usd_metadata(mesh_instance, "usd:material_bindings", material_bindings);
					}
				}
				Array mapping_note_keys = mapping_notes.keys();
				for (int i = 0; i < mapping_note_keys.size(); i++) {
					const Variant key = mapping_note_keys[i];
					if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
						set_usd_metadata(mesh_instance, String(key), mapping_notes[key]);
					}
				}
			}
			node = primitive;
		} else if (p_prim.IsA<UsdGeomCamera>()) {
			Camera3D *camera = memnew(Camera3D);
			UsdGeomCamera usd_camera(p_prim);
			TfToken projection = UsdGeomTokens->perspective;
			usd_camera.GetProjectionAttr().Get(&projection, time);
			if (projection == UsdGeomTokens->orthographic) {
				camera->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
				double horizontal_aperture = 20.955;
				usd_camera.GetHorizontalApertureAttr().Get(&horizontal_aperture, time);
				camera->set_size((real_t)horizontal_aperture);
			} else {
				camera->set_projection(Camera3D::PROJECTION_PERSPECTIVE);
			}

			GfVec2f clipping_range(0.1f, 4000.0f);
			if (usd_camera.GetClippingRangeAttr().Get(&clipping_range, time)) {
				camera->set_near(clipping_range[0]);
				camera->set_far(clipping_range[1]);
			}
			node = camera;
		} else if (p_prim.IsA<UsdLuxDistantLight>()) {
			DirectionalLight3D *light = memnew(DirectionalLight3D);
			UsdLuxDistantLight usd_light(p_prim);
			apply_light_attributes(usd_light, light);
			node = light;
		} else if (p_prim.IsA<UsdLuxSphereLight>()) {
			UsdLuxSphereLight usd_light(p_prim);
			float radius = 1.0f;
			usd_light.GetRadiusAttr().Get(&radius, time);

			SpotLight3D *spot_light = memnew(SpotLight3D);
			if (apply_shaping_to_spot_light(p_prim, spot_light)) {
				apply_light_attributes(usd_light, spot_light);
				spot_light->set_param(Light3D::PARAM_SIZE, radius);
				spot_light->set_param(Light3D::PARAM_RANGE, MAX(radius * 20.0f, 1.0f));
				set_usd_metadata(spot_light, "usd:light_mapping", "UsdLuxSphereLight with ShapingAPI was approximated as SpotLight3D.");
				node = spot_light;
			} else {
				memdelete(spot_light);
				OmniLight3D *light = memnew(OmniLight3D);
				apply_light_attributes(usd_light, light);
				light->set_param(Light3D::PARAM_SIZE, radius);
				light->set_param(Light3D::PARAM_RANGE, MAX(radius * 10.0f, 1.0f));
				node = light;
			}
		} else if (p_prim.IsA<UsdLuxRectLight>()) {
			UsdLuxRectLight rect_light(p_prim);
			GfVec3f color(1.0f, 1.0f, 1.0f);
			float intensity = 1.0f;
			float exposure = 0.0f;
			float width = 1.0f;
			float height = 1.0f;
			rect_light.GetColorAttr().Get(&color, time);
			rect_light.GetIntensityAttr().Get(&intensity, time);
			rect_light.GetExposureAttr().Get(&exposure, time);
			rect_light.GetWidthAttr().Get(&width, time);
			rect_light.GetHeightAttr().Get(&height, time);

			Ref<Texture2D> area_texture = load_texture_from_asset_attribute(stage, time, rect_light.GetTextureFileAttr());
			const Color light_color(color[0], color[1], color[2], 1.0f);
			const float light_intensity = intensity * Math::pow(2.0f, exposure);
			Dictionary mapping_notes;
			if (area_texture.is_valid()) {
				mapping_notes["usd:rect_light_has_texture"] = true;
			}

			node = build_area_light3d_dynamic(Vector2(width, height), area_texture, MAX(MAX(width, height) * 10.0f, 1.0f), light_color, light_intensity, mapping_notes);
			if (node == nullptr) {
				mapping_notes["usd:light_mapping"] = "UsdLuxRectLight was mapped to UsdAreaLightProxy because AreaLight3D is unavailable in this runtime.";
				node = build_area_light_proxy("UsdLuxRectLight", "rect", Vector2(width, height), area_texture, MAX(MAX(width, height) * 10.0f, 1.0f), light_color, light_intensity, mapping_notes);
			}
		} else if (p_prim.IsA<UsdLuxDiskLight>()) {
			UsdLuxDiskLight disk_light(p_prim);
			GfVec3f color(1.0f, 1.0f, 1.0f);
			float intensity = 1.0f;
			float exposure = 0.0f;
			float radius = 0.5f;
			disk_light.GetColorAttr().Get(&color, time);
			disk_light.GetIntensityAttr().Get(&intensity, time);
			disk_light.GetExposureAttr().Get(&exposure, time);
			disk_light.GetRadiusAttr().Get(&radius, time);

			const Color light_color(color[0], color[1], color[2], 1.0f);
			const float light_intensity = intensity * Math::pow(2.0f, exposure);
			Dictionary mapping_notes;
			mapping_notes["usd:light_mapping"] = "UsdLuxDiskLight was approximated as AreaLight3D.";

			node = build_area_light3d_dynamic(Vector2(radius * 2.0f, radius * 2.0f), Ref<Texture2D>(), MAX(radius * 10.0f, 1.0f), light_color, light_intensity, mapping_notes);
			if (node == nullptr) {
				mapping_notes["usd:light_mapping"] = "UsdLuxDiskLight was mapped to UsdAreaLightProxy because AreaLight3D is unavailable in this runtime.";
				node = build_area_light_proxy("UsdLuxDiskLight", "disk", Vector2(radius * 2.0f, radius * 2.0f), Ref<Texture2D>(), MAX(radius * 10.0f, 1.0f), light_color, light_intensity, mapping_notes);
			}
		} else if (p_prim.IsA<UsdLuxCylinderLight>()) {
			UsdLuxCylinderLight cylinder_light(p_prim);
			float radius = 0.5f;
			float length = 1.0f;
			cylinder_light.GetRadiusAttr().Get(&radius, time);
			cylinder_light.GetLengthAttr().Get(&length, time);

			SpotLight3D *spot_light = memnew(SpotLight3D);
			if (apply_shaping_to_spot_light(p_prim, spot_light)) {
				apply_light_attributes(cylinder_light, spot_light);
				spot_light->set_param(Light3D::PARAM_SIZE, radius);
				spot_light->set_param(Light3D::PARAM_RANGE, MAX(length * 10.0f, 1.0f));
				set_usd_metadata(spot_light, "usd:light_mapping", "UsdLuxCylinderLight with ShapingAPI was approximated as SpotLight3D.");
				node = spot_light;
			} else {
				memdelete(spot_light);
				OmniLight3D *light = memnew(OmniLight3D);
				apply_light_attributes(cylinder_light, light);
				light->set_param(Light3D::PARAM_SIZE, radius);
				light->set_param(Light3D::PARAM_RANGE, MAX(MAX(length, radius) * 10.0f, 1.0f));
				set_usd_metadata(light, "usd:light_mapping", "UsdLuxCylinderLight was approximated as OmniLight3D.");
				node = light;
			}
		} else if (p_prim.IsA<UsdGeomBasisCurves>()) {
			Dictionary mapping_notes;
			Node3D *basis_root = build_basis_curves_node(p_prim, time, &mapping_notes);
			Array mapping_note_keys = mapping_notes.keys();
			for (int i = 0; i < mapping_note_keys.size(); i++) {
				const Variant key = mapping_note_keys[i];
				if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
					set_usd_metadata(basis_root, String(key), mapping_notes[key]);
				}
			}
			node = basis_root;
		} else if (p_prim.IsA<UsdSkelSkeleton>()) {
			Dictionary mapping_notes;
			Skeleton3D *skeleton = build_skeleton_node(stage, time, get_stage_correction_transform(), p_prim, &mapping_notes);
			Array mapping_note_keys = mapping_notes.keys();
			for (int i = 0; i < mapping_note_keys.size(); i++) {
				const Variant key = mapping_note_keys[i];
				if (key.get_type() == Variant::STRING || key.get_type() == Variant::STRING_NAME) {
					set_usd_metadata(skeleton, String(key), mapping_notes[key]);
				}
			}
			node = skeleton;
		} else if (p_prim.IsA<UsdGeomXformable>() || p_prim.IsA<UsdGeomImageable>()) {
			node = memnew(Node3D);
		} else {
			node = memnew(Node);
		}

		node->set_name(to_godot_string(p_prim.GetName().GetString()));
		set_usd_metadata_entries(node, make_common_metadata(p_prim));
		store_unmapped_properties(p_prim, time, node);
		store_composition_arcs(p_prim, node);
		apply_transform_and_visibility(p_prim, node);

		for (const UsdPrim &child_prim : p_prim.GetChildren()) {
			if (should_skip_child_prim(child_prim)) {
				continue;
			}
			Node *child_node = build_node(child_prim);
			if (child_node == nullptr) {
				continue;
			}
			node->add_child(child_node);
		}

		return node;
	}

public:
	explicit UsdSceneBuilder(const UsdStageRefPtr &p_stage) :
			stage(p_stage),
			meters_per_unit(p_stage != nullptr ? UsdGeomGetStageMetersPerUnit(p_stage) : 1.0),
			up_axis(p_stage != nullptr ? UsdGeomGetStageUpAxis(p_stage) : TfToken()),
			variant_catalog(collect_variant_sets(p_stage)) {
	}

	Node *build(const String &p_root_name) const {
		ERR_FAIL_COND_V(stage == nullptr, nullptr);
		Node3D *root = memnew(Node3D);
		root->set_name(p_root_name);
		root->set_transform(get_stage_correction_transform());
		Dictionary stage_metadata = collect_stage_metadata(stage);
		const bool has_authored_lights = stage_has_authored_lights();
		const UsdPreviewLightingMode preview_lighting_mode = get_preview_lighting_mode();
		const bool add_preview_lighting = preview_lighting_mode == USD_PREVIEW_LIGHTING_ALWAYS || (preview_lighting_mode == USD_PREVIEW_LIGHTING_WHEN_MISSING && !has_authored_lights);
		stage_metadata["usd:has_authored_lights"] = has_authored_lights;
		stage_metadata["usd:preview_lighting_mode"] = preview_lighting_mode_to_string(preview_lighting_mode);
		stage_metadata["usd:has_preview_lighting"] = add_preview_lighting;
		set_usd_metadata_entries(root, stage_metadata);

		UsdPrim pseudo_root = stage->GetPseudoRoot();
		for (const UsdPrim &child_prim : pseudo_root.GetChildren()) {
			if (should_skip_child_prim(child_prim)) {
				continue;
			}
			Node *child_node = build_node(child_prim);
			if (child_node != nullptr) {
				root->add_child(child_node);
			}
		}

		if (add_preview_lighting) {
			const String preview_reason = preview_lighting_mode == USD_PREVIEW_LIGHTING_ALWAYS
					? String("Synthetic preview lighting was forced by project setting '") + USD_PREVIEW_LIGHTING_MODE_SETTING + "'."
					: "No authored UsdLux lights were found on the USD stage.";
			append_preview_lighting(root, preview_reason);
		}

		append_skin_bindings(root);
		append_baked_skeleton_animations(stage, time, root);

		return root;
	}
};

void warn_source_stage_instance_composition_boundary_edits(Node *p_stage_instance_root, const String &p_source_path, const Dictionary &p_variant_selections) {
	ERR_FAIL_NULL(p_stage_instance_root);

	Node *generated_root = nullptr;
	for (int i = 0; i < p_stage_instance_root->get_child_count(false); i++) {
		Node *child = p_stage_instance_root->get_child(i, false);
		if (child->has_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META))) {
			generated_root = child;
			break;
		}
	}
	if (generated_root == nullptr) {
		return;
	}

	UsdStageRefPtr expected_stage = open_stage_for_instance(p_source_path, p_variant_selections);
	if (!expected_stage) {
		return;
	}

	UsdSceneBuilder builder(expected_stage);
	Node *expected_root = builder.build("_Generated");
	if (expected_root == nullptr) {
		return;
	}

	std::unordered_map<std::string, Node *> current_nodes;
	std::unordered_map<std::string, Node *> expected_nodes;
	collect_generated_composition_boundary_nodes(generated_root, false, &current_nodes, nullptr);
	collect_generated_composition_boundary_nodes(expected_root, false, &expected_nodes, nullptr);
	if (current_nodes.empty()) {
		memdelete(expected_root);
		return;
	}

	int warning_count = 0;
	const int warning_limit = 12;
	const auto warn_once = [&](const String &p_message) {
		if (warning_count >= warning_limit) {
			return;
		}
		UtilityFunctions::push_warning(vformat("USD source-preserving save detected generated edits below a composition boundary in %s: %s", p_source_path, p_message));
		warning_count++;
	};

	for (const auto &entry : current_nodes) {
		const auto expected_it = expected_nodes.find(entry.first);
		if (expected_it == expected_nodes.end() || expected_it->second == nullptr || entry.second == nullptr) {
			continue;
		}

		String mismatch_reason;
		if (!generated_node_state_matches(entry.second, expected_it->second, &mismatch_reason)) {
			warn_once(vformat("prim %s has a generated-node edit that cannot be merged into preserved USD composition (%s).", to_godot_string(entry.first), mismatch_reason));
		}
	}

	if (warning_count == warning_limit) {
		UtilityFunctions::push_warning(vformat("USD source-preserving save detected additional generated edits below composition boundaries in %s; further warnings were suppressed.", p_source_path));
	}

	memdelete(expected_root);
}

} // namespace

void UsdStageResource::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_source_path", "source_path"), &UsdStageResource::set_source_path);
	ClassDB::bind_method(D_METHOD("get_source_path"), &UsdStageResource::get_source_path);
	ClassDB::bind_method(D_METHOD("get_stage_metadata"), &UsdStageResource::get_stage_metadata);
	ClassDB::bind_method(D_METHOD("get_variant_sets"), &UsdStageResource::get_variant_sets);
	ClassDB::bind_method(D_METHOD("refresh_metadata"), &UsdStageResource::refresh_metadata);

	ADD_PROPERTY(PropertyInfo(Variant::STRING, "source_path", PROPERTY_HINT_FILE, "*.usd,*.usda,*.usdc,*.usdz"), "set_source_path", "get_source_path");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "stage_metadata", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "", "get_stage_metadata");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variant_sets", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NONE), "", "get_variant_sets");
}

void UsdStageResource::set_source_path(const String &p_source_path) {
	if (source_path == p_source_path) {
		return;
	}
	source_path = p_source_path;
	refresh_metadata();
}

String UsdStageResource::get_source_path() const {
	return source_path;
}

Dictionary UsdStageResource::get_stage_metadata() const {
	return stage_metadata;
}

Dictionary UsdStageResource::get_variant_sets() const {
	return variant_sets;
}

Error UsdStageResource::refresh_metadata() {
	stage_metadata.clear();
	variant_sets.clear();

	if (source_path.is_empty()) {
		notify_property_list_changed();
		return ERR_UNCONFIGURED;
	}

	if (!FileAccess::file_exists(source_path)) {
		notify_property_list_changed();
		return ERR_FILE_NOT_FOUND;
	}

	UsdStageRefPtr stage_ptr = open_stage_for_instance(source_path);
	if (!stage_ptr) {
		notify_property_list_changed();
		return ERR_CANT_OPEN;
	}

	stage_metadata = collect_stage_metadata(stage_ptr);
	variant_sets = collect_variant_sets(stage_ptr);
	notify_property_list_changed();
	emit_changed();
	return OK;
}

void UsdStageInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_stage", "stage"), &UsdStageInstance::set_stage);
	ClassDB::bind_method(D_METHOD("get_stage"), &UsdStageInstance::get_stage);
	ClassDB::bind_method(D_METHOD("set_variant_selections", "variant_selections"), &UsdStageInstance::set_variant_selections);
	ClassDB::bind_method(D_METHOD("get_variant_selections"), &UsdStageInstance::get_variant_selections);
	ClassDB::bind_method(D_METHOD("set_runtime_node_overrides", "runtime_node_overrides"), &UsdStageInstance::set_runtime_node_overrides);
	ClassDB::bind_method(D_METHOD("get_runtime_node_overrides"), &UsdStageInstance::get_runtime_node_overrides);
	ClassDB::bind_method(D_METHOD("set_debug_logging", "debug_logging"), &UsdStageInstance::set_debug_logging);
	ClassDB::bind_method(D_METHOD("is_debug_logging"), &UsdStageInstance::is_debug_logging);
	ClassDB::bind_method(D_METHOD("get_debug_rebuild_count"), &UsdStageInstance::get_debug_rebuild_count);
	ClassDB::bind_method(D_METHOD("get_debug_last_selection_change"), &UsdStageInstance::get_debug_last_selection_change);
	ClassDB::bind_method(D_METHOD("get_debug_last_rebuild_status"), &UsdStageInstance::get_debug_last_rebuild_status);
	ClassDB::bind_method(D_METHOD("get_debug_last_generated_summary"), &UsdStageInstance::get_debug_last_generated_summary);
	ClassDB::bind_method(D_METHOD("rebuild"), &UsdStageInstance::rebuild);
	ClassDB::bind_method(D_METHOD("get_node_for_prim_path", "prim_path"), &UsdStageInstance::get_node_for_prim_path);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "stage", PROPERTY_HINT_RESOURCE_TYPE, "UsdStageResource"), "set_stage", "get_stage");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "variant_selections", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NO_EDITOR), "set_variant_selections", "get_variant_selections");
	ADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "runtime_node_overrides", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NO_EDITOR), "set_runtime_node_overrides", "get_runtime_node_overrides");
	ADD_GROUP("USD Debug", "debug_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_logging"), "set_debug_logging", "is_debug_logging");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_rebuild_count", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_rebuild_count");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_selection_change", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_selection_change");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_rebuild_status", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_rebuild_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_generated_summary", PROPERTY_HINT_MULTILINE_TEXT, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_generated_summary");
}

void UsdStageInstance::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (!rebuilt_after_scene_instantiation && stage.is_valid() && !stage->get_source_path().is_empty()) {
			rebuild();
		}
	}
}

UsdStageInstance::~UsdStageInstance() {
	if (stage.is_valid() && stage->is_connected("changed", callable_mp(this, &UsdStageInstance::_stage_changed))) {
		stage->disconnect("changed", callable_mp(this, &UsdStageInstance::_stage_changed));
	}
	_clear_generated_children();
	stage.unref();
}

void UsdStageInstance::_clear_node_children(Node *p_node) {
	ERR_FAIL_NULL(p_node);
	for (int i = p_node->get_child_count(true) - 1; i >= 0; i--) {
		Node *child = p_node->get_child(i, true);
		p_node->remove_child(child);
		free_node_immediate(child);
	}
}

void UsdStageInstance::_clear_generated_children() {
	for (int i = get_child_count(true) - 1; i >= 0; i--) {
		Node *child = get_child(i, true);
		if (!_is_generated_root(child)) {
			continue;
		}

		remove_child(child);
		free_node_immediate(child);
	}
	generated_root = nullptr;
}

bool UsdStageInstance::_is_generated_root(Node *p_node) const {
	if (p_node == nullptr) {
		return false;
	}
	if (p_node->has_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META)) && (bool)p_node->get_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META))) {
		return true;
	}
	return p_node->get_name() == StringName("_Generated");
}

void UsdStageInstance::_adopt_existing_generated_root() {
	if (generated_root != nullptr && generated_root->get_parent() == this && _is_generated_root(generated_root)) {
		generated_root->set_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META), true);
	} else {
		generated_root = nullptr;
	}

	if (generated_root == nullptr) {
		for (int i = 0; i < get_child_count(true); i++) {
			Node *child = get_child(i, true);
			if (!_is_generated_root(child)) {
				continue;
			}
			generated_root = child;
			generated_root->set_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META), true);
			break;
		}
	}

	for (int i = get_child_count(true) - 1; i >= 0; i--) {
		Node *child = get_child(i, true);
		if (child == generated_root || !_is_generated_root(child)) {
			continue;
		}

		remove_child(child);
		free_node_immediate(child);
	}
}

Node *UsdStageInstance::_get_generated_owner() const {
	if (is_inside_tree()) {
		SceneTree *tree = get_tree();
		Node *edited_scene_root = tree != nullptr ? tree->get_edited_scene_root() : nullptr;
		if (edited_scene_root != nullptr && (edited_scene_root == this || edited_scene_root->is_ancestor_of(const_cast<UsdStageInstance *>(this)))) {
			return edited_scene_root;
		}
	}

	return get_owner();
}

void UsdStageInstance::_mark_generated_tree_owned() {
	if (generated_root == nullptr) {
		return;
	}

	Node *owner = _get_generated_owner();
	if (owner == nullptr) {
		return;
	}

	mark_owner_recursive(generated_root, owner);
}

Node *UsdStageInstance::_find_node_for_prim_path(Node *p_node, const String &p_prim_path) const {
	ERR_FAIL_NULL_V(p_node, nullptr);
	if (_get_prim_path_for_node(p_node) == p_prim_path) {
		return p_node;
	}
	for (int i = 0; i < p_node->get_child_count(false); i++) {
		Node *found = _find_node_for_prim_path(p_node->get_child(i, false), p_prim_path);
		if (found != nullptr) {
			return found;
		}
	}
	return nullptr;
}

void UsdStageInstance::_append_generated_summary(Node *p_node, PackedStringArray *r_summary, int p_limit) const {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_summary);
	if (r_summary->size() >= p_limit) {
		return;
	}

	const Dictionary metadata = get_usd_metadata(p_node);
	const String prim_path = metadata.get("usd:prim_path", String());
	if (!prim_path.is_empty()) {
		r_summary->push_back(prim_path);
	}

	for (int i = 0; i < p_node->get_child_count(false) && r_summary->size() < p_limit; i++) {
		_append_generated_summary(p_node->get_child(i, false), r_summary, p_limit);
	}
}

String UsdStageInstance::_get_generated_summary() const {
	if (generated_root == nullptr) {
		return "<no generated root>";
	}
	PackedStringArray summary;
	_append_generated_summary(generated_root, &summary, 12);
	if (summary.is_empty()) {
		return "<generated root has no USD prim nodes>";
	}
	return String(", ").join(summary);
}

String UsdStageInstance::_get_prim_path_for_node(const Node *p_node) const {
	ERR_FAIL_NULL_V(p_node, String());
	const Dictionary metadata = get_usd_metadata(p_node);
	return metadata.get("usd:prim_path", String());
}

bool UsdStageInstance::_get_node3d_runtime_state(Node *p_node, Dictionary *r_state) const {
	ERR_FAIL_NULL_V(p_node, false);
	ERR_FAIL_NULL_V(r_state, false);

	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d == nullptr) {
		return false;
	}

	const String prim_path = _get_prim_path_for_node(p_node);
	if (prim_path.is_empty()) {
		return false;
	}

	Dictionary state;
	state["transform"] = node_3d->get_transform();
	state["visible"] = node_3d->is_visible();
	*r_state = state;
	return true;
}

bool UsdStageInstance::_node3d_runtime_state_matches(Node *p_node, const Dictionary &p_state) const {
	ERR_FAIL_NULL_V(p_node, false);

	Node3D *node_3d = Object::cast_to<Node3D>(p_node);
	if (node_3d == nullptr) {
		return false;
	}

	if (p_state.get("transform", Variant()).get_type() != Variant::TRANSFORM3D) {
		return false;
	}
	if (!transforms_equal_approx(node_3d->get_transform(), p_state["transform"])) {
		return false;
	}

	if (p_state.get("visible", Variant()).get_type() != Variant::BOOL) {
		return false;
	}
	return node_3d->is_visible() == (bool)p_state["visible"];
}

void UsdStageInstance::_collect_runtime_node_baselines(Node *p_node, Dictionary *r_baselines) const {
	ERR_FAIL_NULL(p_node);
	ERR_FAIL_NULL(r_baselines);

	Dictionary state;
	if (_get_node3d_runtime_state(p_node, &state)) {
		r_baselines->set(_get_prim_path_for_node(p_node), state);
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		_collect_runtime_node_baselines(p_node->get_child(i, false), r_baselines);
	}
}

void UsdStageInstance::_capture_runtime_node_overrides() {
	if (generated_root == nullptr) {
		return;
	}

	Dictionary current_states;
	_collect_runtime_node_baselines(generated_root, &current_states);
	Array prim_keys = current_states.keys();
	for (int i = 0; i < prim_keys.size(); i++) {
		const String prim_path = prim_keys[i];
		const Variant current_state_variant = current_states[prim_path];
		if (current_state_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const Dictionary current_state = current_state_variant;
		const Variant baseline_variant = generated_node_baselines.get(prim_path, Variant());
		if (baseline_variant.get_type() != Variant::DICTIONARY) {
			runtime_node_overrides[prim_path] = current_state;
			continue;
		}

		Node *current_node = _find_node_for_prim_path(generated_root, prim_path);
		if (current_node != nullptr && !_node3d_runtime_state_matches(current_node, baseline_variant)) {
			runtime_node_overrides[prim_path] = current_state;
		} else {
			runtime_node_overrides.erase(prim_path);
		}
	}
}

void UsdStageInstance::_refresh_runtime_node_baselines() {
	generated_node_baselines.clear();
	if (generated_root == nullptr) {
		return;
	}

	_collect_runtime_node_baselines(generated_root, &generated_node_baselines);
}

void UsdStageInstance::_apply_runtime_node_overrides(Node *p_node) {
	ERR_FAIL_NULL(p_node);

	const String prim_path = _get_prim_path_for_node(p_node);
	if (!prim_path.is_empty()) {
		const Variant override_variant = runtime_node_overrides.get(prim_path, Variant());
		if (override_variant.get_type() == Variant::DICTIONARY) {
			Node3D *node_3d = Object::cast_to<Node3D>(p_node);
			if (node_3d != nullptr) {
				const Dictionary override_state = override_variant;
				if (override_state.get("transform", Variant()).get_type() == Variant::TRANSFORM3D) {
					node_3d->set_transform(override_state["transform"]);
				}
				if (override_state.get("visible", Variant()).get_type() == Variant::BOOL) {
					node_3d->set_visible((bool)override_state["visible"]);
				}
			}
		}
	}

	for (int i = 0; i < p_node->get_child_count(false); i++) {
		_apply_runtime_node_overrides(p_node->get_child(i, false));
	}
}

bool UsdStageInstance::_parse_variant_property(const String &p_property, String *r_prim_path, String *r_variant_set) const {
	if (!p_property.begins_with("variants/")) {
		return false;
	}

	const String variant_path = p_property.substr(9);
	const int separator = variant_path.rfind("/");
	if (separator <= 0 || separator >= variant_path.length() - 1) {
		return false;
	}

	const String property_prim_path = variant_path.substr(0, separator);
	if (r_prim_path != nullptr) {
		*r_prim_path = property_prim_path == "_root" ? "/" : "/" + property_prim_path;
	}
	if (r_variant_set != nullptr) {
		*r_variant_set = variant_path.substr(separator + 1);
	}
	return true;
}

String UsdStageInstance::_get_variant_selection(const String &p_prim_path, const String &p_variant_set) const {
	const Variant explicit_variant = variant_selections.get(p_prim_path, Variant());
	if (explicit_variant.get_type() == Variant::DICTIONARY) {
		const Dictionary prim_variants = explicit_variant;
		const Variant selection = prim_variants.get(p_variant_set, Variant());
		if (selection.get_type() == Variant::STRING || selection.get_type() == Variant::STRING_NAME) {
			return selection;
		}
	}

	const Dictionary variant_sets = !composed_variant_sets.is_empty() ? composed_variant_sets : (stage.is_valid() ? stage->get_variant_sets() : Dictionary());
	const Variant prim_sets_variant = variant_sets.get(p_prim_path, Variant());
	if (prim_sets_variant.get_type() == Variant::DICTIONARY) {
		const Dictionary prim_sets = prim_sets_variant;
		const Variant set_description_variant = prim_sets.get(p_variant_set, Variant());
		if (set_description_variant.get_type() == Variant::DICTIONARY) {
			const Dictionary set_description = set_description_variant;
			return set_description.get("selection", String());
		}
	}

	return String();
}

void UsdStageInstance::_set_variant_selection_property(const String &p_prim_path, const String &p_variant_set, const String &p_selection) {
	const String current_selection = _get_variant_selection(p_prim_path, p_variant_set);
	debug_last_selection_change = vformat("%s:%s %s -> %s", p_prim_path, p_variant_set, current_selection, p_selection);
	if (current_selection == p_selection) {
		debug_last_rebuild_status = "Skipped rebuild because selection was unchanged.";
		notify_property_list_changed();
		return;
	}

	Dictionary updated_selections = variant_selections.duplicate(true);
	Dictionary prim_selections;
	const Variant existing = updated_selections.get(p_prim_path, Variant());
	if (existing.get_type() == Variant::DICTIONARY) {
		prim_selections = existing;
	}
	prim_selections[p_variant_set] = p_selection;
	updated_selections[p_prim_path] = prim_selections;
	variant_selections = updated_selections;

	if (stage.is_valid() && !stage->get_source_path().is_empty()) {
		rebuild();
	}
	notify_property_list_changed();
}

void UsdStageInstance::_stage_changed() {
	notify_property_list_changed();
	if (stage.is_null() || stage->get_source_path().is_empty()) {
		composed_variant_sets.clear();
		generated_node_baselines.clear();
		runtime_node_overrides.clear();
		skip_next_runtime_override_capture = true;
		_clear_generated_children();
		rebuilt_after_scene_instantiation = false;
		return;
	}

	if (is_inside_tree() || generated_root != nullptr) {
		rebuild();
	} else {
		composed_variant_sets.clear();
		debug_last_rebuild_status = "Deferred rebuild until the instance enters the scene tree.";
	}
}

bool UsdStageInstance::_set(const StringName &p_name, const Variant &p_value) {
	String prim_path;
	String variant_set;
	if (!_parse_variant_property(String(p_name), &prim_path, &variant_set)) {
		return false;
	}
	if (p_value.get_type() != Variant::STRING && p_value.get_type() != Variant::STRING_NAME) {
		return false;
	}
	_set_variant_selection_property(prim_path, variant_set, p_value);
	return true;
}

bool UsdStageInstance::_get(const StringName &p_name, Variant &r_ret) const {
	String prim_path;
	String variant_set;
	if (!_parse_variant_property(String(p_name), &prim_path, &variant_set)) {
		return false;
	}
	r_ret = _get_variant_selection(prim_path, variant_set);
	return true;
}

void UsdStageInstance::_get_property_list(List<PropertyInfo> *p_list) const {
	if (stage.is_null()) {
		return;
	}

	const Dictionary variant_sets = !composed_variant_sets.is_empty() ? composed_variant_sets : stage->get_variant_sets();
	Array prim_keys = variant_sets.keys();
	if (!prim_keys.is_empty()) {
		p_list->push_back(PropertyInfo(Variant::NIL, "USD Variants", PROPERTY_HINT_NONE, "variants/", PROPERTY_USAGE_GROUP));
	}

	for (int i = 0; i < prim_keys.size(); i++) {
		const String prim_path = prim_keys[i];
		const Variant prim_sets_variant = variant_sets[prim_path];
		if (prim_sets_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}

		String property_prim_path = prim_path.trim_prefix("/");
		if (property_prim_path.is_empty()) {
			property_prim_path = "_root";
		}

		const Dictionary prim_sets = prim_sets_variant;
		Array set_keys = prim_sets.keys();
		for (int set_index = 0; set_index < set_keys.size(); set_index++) {
			const String variant_set = set_keys[set_index];
			const Variant set_description_variant = prim_sets[variant_set];
			if (set_description_variant.get_type() != Variant::DICTIONARY) {
				continue;
			}

			const Dictionary set_description = set_description_variant;
			const Array variants = set_description.get("variants", Array());
			if (variants.is_empty()) {
				continue;
			}

			String hint_string;
			for (int variant_index = 0; variant_index < variants.size(); variant_index++) {
				const Variant variant_name = variants[variant_index];
				if (variant_name.get_type() != Variant::STRING && variant_name.get_type() != Variant::STRING_NAME) {
					continue;
				}
				if (!hint_string.is_empty()) {
					hint_string += ",";
				}
				hint_string += String(variant_name);
			}

			if (hint_string.is_empty()) {
				continue;
			}

			p_list->push_back(PropertyInfo(
					Variant::STRING,
					"variants/" + property_prim_path + "/" + variant_set,
					PROPERTY_HINT_ENUM,
					hint_string,
					PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED));
		}
	}
}

void UsdStageInstance::set_stage(const Ref<UsdStageResource> &p_stage) {
	if (stage == p_stage) {
		return;
	}

	if (stage.is_valid() && stage->is_connected("changed", callable_mp(this, &UsdStageInstance::_stage_changed))) {
		stage->disconnect("changed", callable_mp(this, &UsdStageInstance::_stage_changed));
	}

	stage = p_stage;
	rebuilt_after_scene_instantiation = false;
	generated_node_baselines.clear();
	runtime_node_overrides.clear();
	if (stage.is_valid()) {
		stage->connect("changed", callable_mp(this, &UsdStageInstance::_stage_changed));
	}
	_stage_changed();
}

Ref<UsdStageResource> UsdStageInstance::get_stage() const {
	return stage;
}

void UsdStageInstance::set_variant_selections(const Dictionary &p_variant_selections) {
	variant_selections = p_variant_selections;
	rebuilt_after_scene_instantiation = false;
	generated_node_baselines.clear();
	runtime_node_overrides.clear();
	debug_last_selection_change = "variant_selections dictionary replaced";
	if ((is_inside_tree() || generated_root != nullptr) && stage.is_valid() && !stage->get_source_path().is_empty()) {
		rebuild();
	} else if (stage.is_valid() && !stage->get_source_path().is_empty()) {
		debug_last_rebuild_status = "Deferred rebuild until the instance enters the scene tree.";
	}
	notify_property_list_changed();
}

Dictionary UsdStageInstance::get_variant_selections() const {
	return variant_selections;
}

void UsdStageInstance::set_runtime_node_overrides(const Dictionary &p_runtime_node_overrides) {
	runtime_node_overrides = p_runtime_node_overrides.duplicate(true);
}

Dictionary UsdStageInstance::get_runtime_node_overrides() const {
	if (generated_root == nullptr) {
		return runtime_node_overrides;
	}

	Dictionary current_states;
	_collect_runtime_node_baselines(generated_root, &current_states);
	Dictionary overrides = runtime_node_overrides.duplicate(true);
	Array prim_keys = current_states.keys();
	for (int i = 0; i < prim_keys.size(); i++) {
		const String prim_path = prim_keys[i];
		const Variant current_state_variant = current_states[prim_path];
		if (current_state_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const Dictionary current_state = current_state_variant;
		const Variant baseline_variant = generated_node_baselines.get(prim_path, Variant());
		if (baseline_variant.get_type() != Variant::DICTIONARY) {
			overrides[prim_path] = current_state;
			continue;
		}

		Node *current_node = _find_node_for_prim_path(generated_root, prim_path);
		if (current_node != nullptr && !_node3d_runtime_state_matches(current_node, baseline_variant)) {
			overrides[prim_path] = current_state;
		} else {
			overrides.erase(prim_path);
		}
	}
	return overrides;
}

void UsdStageInstance::set_debug_logging(bool p_debug_logging) {
	debug_logging = p_debug_logging;
}

bool UsdStageInstance::is_debug_logging() const {
	return debug_logging;
}

int UsdStageInstance::get_debug_rebuild_count() const {
	return debug_rebuild_count;
}

String UsdStageInstance::get_debug_last_selection_change() const {
	return debug_last_selection_change;
}

String UsdStageInstance::get_debug_last_rebuild_status() const {
	return debug_last_rebuild_status;
}

String UsdStageInstance::get_debug_last_generated_summary() const {
	return debug_last_generated_summary;
}

Error UsdStageInstance::rebuild() {
	if (skip_next_runtime_override_capture) {
		skip_next_runtime_override_capture = false;
	} else {
		_capture_runtime_node_overrides();
	}
	composed_variant_sets.clear();
	debug_rebuild_count++;
	debug_last_rebuild_status = vformat("Rebuild #%d started.", debug_rebuild_count);

	if (stage.is_null()) {
		debug_last_rebuild_status = "Rebuild failed: instance requires a stage resource.";
		return ERR_UNCONFIGURED;
	}
	if (stage->get_source_path().is_empty()) {
		debug_last_rebuild_status = "Rebuild failed: stage resource has no source path.";
		return ERR_UNCONFIGURED;
	}

	UsdStageRefPtr composed_stage = open_stage_for_instance(stage->get_source_path(), variant_selections);
	if (!composed_stage) {
		debug_last_rebuild_status = vformat("Rebuild failed: could not compose USD stage for %s.", stage->get_source_path());
		return ERR_CANT_OPEN;
	}
	composed_variant_sets = collect_variant_sets(composed_stage);

	UsdSceneBuilder builder(composed_stage);
	Node *rebuilt_root = builder.build("_Generated");
	ERR_FAIL_NULL_V(rebuilt_root, ERR_CANT_CREATE);

	_adopt_existing_generated_root();
	if (generated_root == nullptr) {
		generated_root = rebuilt_root;
		generated_root->set_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META), true);
		add_child(generated_root);
	} else {
		_clear_node_children(generated_root);
		while (rebuilt_root->get_child_count(true) > 0) {
			Node *child = rebuilt_root->get_child(0, true);
			rebuilt_root->remove_child(child);
			generated_root->add_child(child);
		}
		if (Node3D *generated_root_3d = Object::cast_to<Node3D>(generated_root)) {
			if (Node3D *rebuilt_root_3d = Object::cast_to<Node3D>(rebuilt_root)) {
				generated_root_3d->set_transform(rebuilt_root_3d->get_transform());
			}
		}
		memdelete(rebuilt_root);
	}

	generated_root->set_meta(StringName("usd_stage_instance_source_path"), stage->get_source_path());
	generated_root->set_meta(StringName("usd_stage_instance_variant_selections"), variant_selections);
	_refresh_runtime_node_baselines();
	_apply_runtime_node_overrides(generated_root);
	_mark_generated_tree_owned();
	debug_last_generated_summary = _get_generated_summary();
	debug_last_rebuild_status = vformat("Rebuild #%d completed: %d generated root children.", debug_rebuild_count, generated_root->get_child_count(false));

	if (debug_logging) {
		UtilityFunctions::print(vformat("UsdStageInstance: %s summary=%s", debug_last_rebuild_status, debug_last_generated_summary));
	}

	return OK;
}

Node *UsdStageInstance::get_node_for_prim_path(const String &p_prim_path) const {
	if (generated_root == nullptr) {
		return nullptr;
	}
	return _find_node_for_prim_path(generated_root, p_prim_path);
}

PackedStringArray UsdSceneFormatLoader::_get_recognized_extensions() const {
	PackedStringArray extensions;
	extensions.push_back("usd");
	extensions.push_back("usda");
	extensions.push_back("usdc");
	extensions.push_back("usdz");
	return extensions;
}

bool UsdSceneFormatLoader::_recognize_path(const String &p_path, const StringName &p_type) const {
	return is_usd_scene_extension(p_path.get_extension()) && _handles_type(p_type);
}

bool UsdSceneFormatLoader::_handles_type(const StringName &p_type) const {
	return String(p_type) == "PackedScene" || String(p_type).is_empty();
}

String UsdSceneFormatLoader::_get_resource_type(const String &p_path) const {
	return is_usd_scene_extension(p_path.get_extension()) ? String("PackedScene") : String();
}

bool UsdSceneFormatLoader::_exists(const String &p_path) const {
	return FileAccess::file_exists(p_path);
}

Variant UsdSceneFormatLoader::_load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	(void)p_original_path;
	(void)p_use_sub_threads;
	(void)p_cache_mode;

	if (!FileAccess::file_exists(p_path)) {
		return Variant();
	}

	UsdStageRefPtr stage_ptr = UsdStage::Open(get_absolute_path(p_path).utf8().get_data(), UsdStage::LoadAll);
	if (!stage_ptr) {
		return Variant();
	}

	Node *scene_root = nullptr;
	const Dictionary variant_sets = collect_variant_sets(stage_ptr);
	if (!variant_sets.is_empty()) {
		Ref<UsdStageResource> stage_resource;
		stage_resource.instantiate();
		stage_resource->set_source_path(p_path);
		if (stage_resource->refresh_metadata() != OK) {
			return Variant();
		}

		UsdStageInstance *stage_instance = memnew(UsdStageInstance);
		stage_instance->set_name(p_path.get_file().get_basename());
		stage_instance->set_stage(stage_resource);
		stage_instance->rebuild();
		scene_root = stage_instance;
	} else {
		UsdSceneBuilder builder(stage_ptr);
		scene_root = builder.build(p_path.get_file().get_basename());
		if (scene_root != nullptr) {
			set_usd_metadata(scene_root, "usd:source_path", p_path);
		}
	}

	if (scene_root == nullptr) {
		return Variant();
	}

	for (int i = 0; i < scene_root->get_child_count(false); i++) {
		mark_owner_recursive(scene_root->get_child(i, false), scene_root);
	}

	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	if (packed_scene->pack(scene_root) != OK) {
		memdelete(scene_root);
		return Variant();
	}
	memdelete(scene_root);
	packed_scene->set_path(p_path);
	return packed_scene;
}

Error UsdSceneFormatSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	(void)p_flags;

	if (!_recognize(p_resource)) {
		return ERR_INVALID_PARAMETER;
	}

	Ref<PackedScene> packed_scene = p_resource;
	ERR_FAIL_COND_V(packed_scene.is_null(), ERR_INVALID_PARAMETER);

	Ref<UsdStageResource> packed_stage_resource;
	Dictionary packed_variant_selections;
	Dictionary packed_runtime_node_overrides;
	if (get_packed_stage_instance_data(packed_scene, &packed_stage_resource, &packed_variant_selections, &packed_runtime_node_overrides)) {
		if (packed_stage_resource.is_null() || packed_stage_resource->get_source_path().is_empty()) {
			UtilityFunctions::push_error("UsdSceneFormatSaver requires a USD-backed stage resource with a source path.");
			return ERR_UNAVAILABLE;
		}

		const String destination_extension = p_path.get_extension().to_lower();
		const String source_absolute_path = get_absolute_path(packed_stage_resource->get_source_path());
		const String source_extension = source_absolute_path.get_extension().to_lower();
		const String destination_absolute_path = get_absolute_path(p_path);

		if (is_usd_scene_extension(destination_extension) && destination_extension == source_extension) {
			warn_packed_stage_instance_runtime_edits(packed_stage_resource->get_source_path(), packed_runtime_node_overrides);

			const Dictionary stage_variant_sets = packed_stage_resource->get_variant_sets();
			if (variant_selections_match_stage_defaults(packed_variant_selections, stage_variant_sets)) {
				const Error copy_error = copy_file_absolute_preserving_contents(source_absolute_path, destination_absolute_path);
				if (copy_error == OK) {
					report_usd_save_mode(vformat("preserved source USD file unchanged: %s -> %s", packed_stage_resource->get_source_path(), p_path));
				}
				return copy_error;
			}

			if (destination_extension == "usdz") {
				const Error save_error = save_source_usdz_with_variant_defaults(source_absolute_path, destination_absolute_path, p_path.get_file(), packed_variant_selections);
				if (save_error == OK) {
					report_usd_save_mode(vformat("authored selected variant defaults into source %s while preserving inactive variant data: %s -> %s", destination_extension.to_upper(), packed_stage_resource->get_source_path(), p_path));
				}
				return save_error;
			}

			if (destination_extension == "usd" || destination_extension == "usda" || destination_extension == "usdc") {
				const Error save_error = save_source_usd_layer_with_variant_defaults(source_absolute_path, destination_absolute_path, p_path.get_file(), packed_variant_selections);
				if (save_error == OK) {
					report_usd_save_mode(vformat("authored selected variant defaults into source %s while preserving inactive variant data: %s -> %s", destination_extension.to_upper(), packed_stage_resource->get_source_path(), p_path));
				}
				return save_error;
			}
		}

		if (destination_extension == "usdz") {
			report_usd_save_mode(vformat("packaging composed Godot scene as USDZ at %s; preserved read-only composition arcs stored on nodes are reauthored, but original package contents, inactive variant branches, and unsupported arcs are not preserved by this path.", p_path), !packed_runtime_node_overrides.is_empty());
		} else {
			report_usd_save_mode(vformat("exporting composed Godot scene to %s; preserved read-only composition arcs stored on nodes are reauthored, but variant sets, inactive branches, and unsupported arcs are not reconstructed by this path.", p_path), !packed_runtime_node_overrides.is_empty());
		}
		return save_composed_usd_stage_to_path(packed_stage_resource->get_source_path(), packed_variant_selections, p_path);
	}

	Node *root = packed_scene->instantiate();
	ERR_FAIL_NULL_V(root, ERR_CANT_CREATE);

	UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(root);
	if (stage_instance == nullptr) {
		const String static_source_path = get_imported_static_scene_source_path(root);
		if (!static_source_path.is_empty()) {
			const String destination_extension = p_path.get_extension().to_lower();
			const String source_absolute_path = get_absolute_path(static_source_path);
			const String source_extension = source_absolute_path.get_extension().to_lower();
			const String destination_absolute_path = get_absolute_path(p_path);
			const bool has_composition_boundaries = node_tree_has_usd_composition_boundaries(root);

			if (has_composition_boundaries) {
				UsdStageRefPtr source_stage = open_stage_for_instance(static_source_path);
				if (!source_stage) {
					free_saver_root(root);
					return ERR_CANT_OPEN;
				}

				report_usd_save_mode(vformat("exporting static imported USD scene to %s while reauthoring preserved read-only composition arcs from %s.", p_path, static_source_path));
				const Error save_error = save_composition_preserving_generated_scene(root, p_path, source_stage);
				free_saver_root(root);
				return save_error;
			}

			if (is_usd_scene_extension(destination_extension) && destination_extension == source_extension) {
				if (destination_extension != "usdz" && node_tree_has_source_aware_static_data(root)) {
					const Error save_error = save_static_imported_data_to_source_copy(root, source_absolute_path, destination_absolute_path);
					if (save_error == OK) {
						report_usd_save_mode(vformat("preserved static imported USD source while merging supported static edits: %s -> %s", static_source_path, p_path));
					}
					free_saver_root(root);
					return save_error;
				}

				const Error copy_error = copy_file_absolute_preserving_contents(source_absolute_path, destination_absolute_path);
				if (copy_error == OK) {
					report_usd_save_mode(vformat("preserved static imported USD source unchanged: %s -> %s", static_source_path, p_path));
				}
				free_saver_root(root);
				return copy_error;
			}

			report_usd_save_mode(vformat("exporting composed static imported USD scene to %s from source %s.", p_path, static_source_path));
			const Error save_error = save_composed_usd_stage_to_path(static_source_path, Dictionary(), p_path);
			free_saver_root(root);
			return save_error;
		}

		const bool has_composition_boundaries = node_tree_has_usd_composition_boundaries(root);
		if (p_path.get_extension().to_lower() == "usdz") {
			report_usd_save_mode(vformat("packaging composed Godot scene as USDZ at %s; preserved read-only composition arcs stored on nodes are reauthored, but original package contents, inactive variant branches, and unsupported arcs are not preserved by this path.", p_path), has_composition_boundaries);
		} else {
			report_usd_save_mode(vformat("exporting composed Godot scene to %s; preserved read-only composition arcs stored on nodes are reauthored, but variant sets, inactive branches, and unsupported arcs are not reconstructed by this path.", p_path), has_composition_boundaries);
		}
		const Error save_error = save_node_tree_to_stage(root, p_path);
		free_saver_root(root);
		return save_error;
	}

	Ref<UsdStageResource> stage_resource = stage_instance->get_stage();
	if (stage_resource.is_null() || stage_resource->get_source_path().is_empty()) {
		free_saver_root(root);
		UtilityFunctions::push_error("UsdSceneFormatSaver requires a USD-backed stage resource with a source path.");
		return ERR_UNAVAILABLE;
	}

	const String destination_extension = p_path.get_extension().to_lower();
	const String source_absolute_path = get_absolute_path(stage_resource->get_source_path());
	const String source_extension = source_absolute_path.get_extension().to_lower();
	const String destination_absolute_path = get_absolute_path(p_path);

	if (is_usd_scene_extension(destination_extension) && destination_extension == source_extension) {
		const Dictionary variant_selections = stage_instance->get_variant_selections();
		const Dictionary stage_variant_sets = stage_resource->get_variant_sets();
		stage_instance->rebuild();
		warn_source_stage_instance_composition_boundary_edits(root, stage_resource->get_source_path(), variant_selections);

		if (variant_selections_match_stage_defaults(variant_selections, stage_variant_sets)) {
			const Error copy_error = copy_file_absolute_preserving_contents(source_absolute_path, destination_absolute_path);
				if (copy_error == OK) {
					report_usd_save_mode(vformat("preserved source USD file unchanged: %s -> %s", stage_resource->get_source_path(), p_path));
				}
				free_saver_root(root);
				return copy_error;
			}

		if (destination_extension == "usdz") {
			const Error save_error = save_source_usdz_with_variant_defaults(source_absolute_path, destination_absolute_path, p_path.get_file(), variant_selections);
				if (save_error == OK) {
					report_usd_save_mode(vformat("authored selected variant defaults into source %s while preserving inactive variant data: %s -> %s", destination_extension.to_upper(), stage_resource->get_source_path(), p_path));
				}
				free_saver_root(root);
				return save_error;
			}

		if (destination_extension == "usd" || destination_extension == "usda" || destination_extension == "usdc") {
			const Error save_error = save_source_usd_layer_with_variant_defaults(source_absolute_path, destination_absolute_path, p_path.get_file(), variant_selections);
				if (save_error == OK) {
					report_usd_save_mode(vformat("authored selected variant defaults into source %s while preserving inactive variant data: %s -> %s", destination_extension.to_upper(), stage_resource->get_source_path(), p_path));
				}
				free_saver_root(root);
				return save_error;
			}
	}

	const bool has_composition_boundaries = node_tree_has_usd_composition_boundaries(root);
	if (destination_extension == "usdz") {
		report_usd_save_mode(vformat("packaging composed Godot scene as USDZ at %s; preserved read-only composition arcs stored on nodes are reauthored, but original package contents, inactive variant branches, and unsupported arcs are not preserved by this path.", p_path), has_composition_boundaries);
		const Error save_error = save_composed_usd_stage_to_path(stage_resource->get_source_path(), stage_instance->get_variant_selections(), p_path);
		free_saver_root(root);
		return save_error;
	}

	report_usd_save_mode(vformat("exporting composed Godot scene to %s; preserved read-only composition arcs stored on nodes are reauthored, but variant sets, inactive branches, and unsupported arcs are not reconstructed by this path.", p_path), has_composition_boundaries);
	const Error save_error = save_composed_usd_stage_to_path(stage_resource->get_source_path(), stage_instance->get_variant_selections(), p_path);
	free_saver_root(root);
	return save_error;
}

bool UsdSceneFormatSaver::_recognize(const Ref<Resource> &p_resource) const {
	return p_resource.is_valid() && p_resource->is_class("PackedScene");
}

PackedStringArray UsdSceneFormatSaver::_get_recognized_extensions(const Ref<Resource> &p_resource) const {
	PackedStringArray extensions;
	if (_recognize(p_resource)) {
		extensions.push_back("usd");
		extensions.push_back("usda");
		extensions.push_back("usdc");
		extensions.push_back("usdz");
	}
	return extensions;
}

bool UsdSceneFormatSaver::_recognize_path(const Ref<Resource> &p_resource, const String &p_path) const {
	return _recognize(p_resource) && is_usd_scene_extension(p_path.get_extension());
}
