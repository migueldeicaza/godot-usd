#include "usd_scene_loader.h"
#include "usd_curves.h"
#include "usd_light_proxy.h"
#include "usd_materials.h"
#include "usd_mesh_builder.h"
#include "usd_skel.h"
#include "usd_stage_utils.h"
#include "usd_usdz.h"

#include <cstring>

#include <godot_cpp/classes/base_material3d.hpp>
#include <godot_cpp/classes/camera3d.hpp>
#include <godot_cpp/classes/directional_light3d.hpp>
#include <godot_cpp/classes/environment.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/global_constants.hpp>
#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/class_db_singleton.hpp>
#include <godot_cpp/classes/light3d.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/omni_light3d.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
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
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <pxr/base/tf/token.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/basisCurves.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/imageable.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/points.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdLux/cylinderLight.h>
#include <pxr/usd/usdLux/diskLight.h>
#include <pxr/usd/usdLux/distantLight.h>
#include <pxr/usd/usdLux/rectLight.h>
#include <pxr/usd/usdLux/shapingAPI.h>
#include <pxr/usd/usdLux/sphereLight.h>
#include <pxr/usd/usdShade/shader.h>
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
	ADD_GROUP("USD Debug", "debug_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "debug_logging"), "set_debug_logging", "is_debug_logging");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_rebuild_count", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_rebuild_count");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_selection_change", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_selection_change");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_rebuild_status", PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_rebuild_status");
	ADD_PROPERTY(PropertyInfo(Variant::STRING, "debug_last_generated_summary", PROPERTY_HINT_MULTILINE_TEXT, String(), PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_READ_ONLY), "", "get_debug_last_generated_summary");
}

void UsdStageInstance::_notification(int p_what) {
	if (p_what == NOTIFICATION_READY) {
		if (stage.is_valid() && !stage->get_source_path().is_empty() && generated_root == nullptr) {
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
	for (int i = p_node->get_child_count() - 1; i >= 0; i--) {
		Node *child = p_node->get_child(i);
		p_node->remove_child(child);
		memdelete(child);
	}
}

void UsdStageInstance::_clear_generated_children() {
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *child = get_child(i);
		if (!child->has_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META))) {
			continue;
		}
		remove_child(child);
		memdelete(child);
	}
	generated_root = nullptr;
}

Node *UsdStageInstance::_find_node_for_prim_path(Node *p_node, const String &p_prim_path) const {
	ERR_FAIL_NULL_V(p_node, nullptr);
	if (_get_prim_path_for_node(p_node) == p_prim_path) {
		return p_node;
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		Node *found = _find_node_for_prim_path(p_node->get_child(i), p_prim_path);
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

	const Variant metadata_variant = p_node->get_meta(StringName("usd"), Variant());
	if (metadata_variant.get_type() == Variant::DICTIONARY) {
		const Dictionary metadata = metadata_variant;
		const String prim_path = metadata.get("usd:prim_path", String());
		if (!prim_path.is_empty()) {
			r_summary->push_back(prim_path);
		}
	}

	for (int i = 0; i < p_node->get_child_count() && r_summary->size() < p_limit; i++) {
		_append_generated_summary(p_node->get_child(i), r_summary, p_limit);
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
	const Variant metadata_variant = p_node->get_meta(StringName("usd"), Variant());
	if (metadata_variant.get_type() != Variant::DICTIONARY) {
		return String();
	}
	const Dictionary metadata = metadata_variant;
	return metadata.get("usd:prim_path", String());
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
		_clear_generated_children();
		return;
	}
	if (!is_inside_tree()) {
		composed_variant_sets.clear();
		return;
	}
	rebuild();
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
	debug_last_selection_change = "variant_selections dictionary replaced";
	if (is_inside_tree() && stage.is_valid() && !stage->get_source_path().is_empty()) {
		rebuild();
	}
	notify_property_list_changed();
}

Dictionary UsdStageInstance::get_variant_selections() const {
	return variant_selections;
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

	if (generated_root == nullptr) {
		generated_root = rebuilt_root;
		generated_root->set_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META), true);
		add_child(generated_root);
	} else {
		_clear_node_children(generated_root);
		while (rebuilt_root->get_child_count() > 0) {
			Node *child = rebuilt_root->get_child(0);
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
	mark_owner_recursive(generated_root, this);
	debug_last_generated_summary = _get_generated_summary();
	debug_last_rebuild_status = vformat("Rebuild #%d completed: %d generated root children.", debug_rebuild_count, generated_root->get_child_count());

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

	Ref<UsdStageResource> stage_resource;
	stage_resource.instantiate();
	stage_resource->set_source_path(p_path);
	if (stage_resource->refresh_metadata() != OK) {
		return Variant();
	}

	UsdStageInstance *stage_instance = memnew(UsdStageInstance);
	stage_instance->set_name(p_path.get_file().get_basename());
	stage_instance->set_stage(stage_resource);

	Ref<PackedScene> packed_scene;
	packed_scene.instantiate();
	if (packed_scene->pack(stage_instance) != OK) {
		memdelete(stage_instance);
		return Variant();
	}
	memdelete(stage_instance);
	packed_scene->set_path(p_path);
	return packed_scene;
}

Error UsdSceneFormatSaver::_save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) {
	(void)p_resource;
	(void)p_path;
	(void)p_flags;
	UtilityFunctions::push_error("UsdSceneFormatSaver is not implemented yet in the GDExtension port.");
	return ERR_UNAVAILABLE;
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
