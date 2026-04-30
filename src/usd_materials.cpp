#include "usd_materials.h"

#include <godot_cpp/classes/standard_material3d.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/math.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/shader.h>

#include "usd_stage_utils.h"
#include "usd_usdz.h"

namespace godot_usd {

using namespace godot;
using namespace pxr;

namespace {

bool extract_texture_uv_transform(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeShader &p_texture_shader, Vector3 *r_uv_scale, Vector3 *r_uv_offset, Dictionary *r_mapping_notes) {
	if (p_stage == nullptr || !p_texture_shader || r_uv_scale == nullptr || r_uv_offset == nullptr) {
		return false;
	}

	UsdShadeInput st_input = p_texture_shader.GetInput(TfToken("st"));
	const UsdShaderConnection st_connection = get_connected_shader(p_stage, st_input);
	if (!st_connection) {
		return false;
	}

	TfToken shader_id;
	if (!get_shader_id(st_connection.shader, p_time, &shader_id) || shader_id != TfToken("UsdTransform2d")) {
		return false;
	}

	GfVec2f scale(1.0f, 1.0f);
	if (UsdShadeInput scale_input = st_connection.shader.GetInput(TfToken("scale"))) {
		scale_input.Get(&scale, p_time);
	}

	GfVec2f translation(0.0f, 0.0f);
	if (UsdShadeInput translation_input = st_connection.shader.GetInput(TfToken("translation"))) {
		translation_input.Get(&translation, p_time);
	}

	float rotation = 0.0f;
	if (UsdShadeInput rotation_input = st_connection.shader.GetInput(TfToken("rotation"))) {
		rotation_input.Get(&rotation, p_time);
	}

	if (!Math::is_zero_approx(rotation) && r_mapping_notes != nullptr) {
		(*r_mapping_notes)["usd:material_uv_transform"] = "UsdTransform2d rotation is not supported by StandardMaterial3D; only scale and translation were applied.";
	}

	*r_uv_scale = Vector3((real_t)scale[0], (real_t)scale[1], 1.0f);
	*r_uv_offset = Vector3((real_t)translation[0], (real_t)(1.0f - scale[1] - translation[1]), 0.0f);
	return true;
}

void merge_material_uv_transform(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeShader &p_texture_shader, StandardMaterial3D *p_material, bool *r_has_uv_transform, Vector3 *r_uv_scale, Vector3 *r_uv_offset, Dictionary *r_mapping_notes) {
	ERR_FAIL_NULL(p_material);
	ERR_FAIL_NULL(r_has_uv_transform);
	ERR_FAIL_NULL(r_uv_scale);
	ERR_FAIL_NULL(r_uv_offset);

	Vector3 uv_scale;
	Vector3 uv_offset;
	if (!extract_texture_uv_transform(p_stage, p_time, p_texture_shader, &uv_scale, &uv_offset, r_mapping_notes)) {
		return;
	}

	if (!*r_has_uv_transform) {
		*r_has_uv_transform = true;
		*r_uv_scale = uv_scale;
		*r_uv_offset = uv_offset;
		p_material->set_uv1_scale(uv_scale);
		p_material->set_uv1_offset(uv_offset);
		return;
	}

	if (!r_uv_scale->is_equal_approx(uv_scale) || !r_uv_offset->is_equal_approx(uv_offset)) {
		if (r_mapping_notes != nullptr) {
			(*r_mapping_notes)["usd:material_uv_transform"] = "Multiple incompatible UsdTransform2d nodes were found; the first transform was kept.";
		}
	}
}

} // namespace

UsdShaderConnection::UsdShaderConnection(const UsdShadeShader &p_shader, const TfToken &p_output_name) :
		shader(p_shader),
		output_name(p_output_name) {}

UsdShaderConnection::operator bool() const {
	return static_cast<bool>(shader);
}

UsdShaderConnection get_connected_shader(const UsdStageRefPtr &p_stage, const UsdShadeInput &p_input) {
	if (p_stage == nullptr || !p_input || !p_input.HasConnectedSource()) {
		return UsdShaderConnection();
	}

	const UsdShadeInput::SourceInfoVector sources = p_input.GetConnectedSources();
	if (sources.empty()) {
		return UsdShaderConnection();
	}

	UsdPrim source_prim = p_stage->GetPrimAtPath(sources[0].source.GetPath());
	if (!source_prim) {
		return UsdShaderConnection();
	}

	UsdShadeShader shader(source_prim);
	if (!shader) {
		return UsdShaderConnection();
	}

	return UsdShaderConnection(shader, sources[0].sourceName);
}

bool get_shader_id(const UsdShadeShader &p_shader, const UsdTimeCode &p_time, TfToken *r_shader_id) {
	if (!p_shader || r_shader_id == nullptr) {
		return false;
	}

	return p_shader.GetPrim().GetAttribute(TfToken("info:id")).Get(r_shader_id, p_time);
}

UsdShaderConnection get_connected_texture_shader(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeInput &p_input) {
	const UsdShaderConnection connection = get_connected_shader(p_stage, p_input);
	if (!connection) {
		return UsdShaderConnection();
	}

	TfToken shader_id;
	if (!get_shader_id(connection.shader, p_time, &shader_id) || shader_id != TfToken("UsdUVTexture")) {
		return UsdShaderConnection();
	}

	return connection;
}

Ref<Image> load_image_from_shader(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeShader &p_shader, String *r_resolved_path) {
	UsdShadeInput file_input = p_shader.GetInput(TfToken("file"));
	if (!file_input) {
		return Ref<Image>();
	}
	return load_image_from_asset_attribute(p_stage, p_time, file_input.GetAttr(), r_resolved_path);
}

Ref<Texture2D> load_texture_from_shader(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeShader &p_shader, String *r_resolved_path) {
	UsdShadeInput file_input = p_shader.GetInput(TfToken("file"));
	if (!file_input) {
		return Ref<Texture2D>();
	}
	return load_texture_from_asset_attribute(p_stage, p_time, file_input.GetAttr(), r_resolved_path);
}

BaseMaterial3D::TextureChannel get_texture_channel_for_output(const TfToken &p_output_name) {
	if (p_output_name == TfToken("r") || p_output_name == TfToken("red")) {
		return BaseMaterial3D::TEXTURE_CHANNEL_RED;
	}
	if (p_output_name == TfToken("g") || p_output_name == TfToken("green")) {
		return BaseMaterial3D::TEXTURE_CHANNEL_GREEN;
	}
	if (p_output_name == TfToken("b") || p_output_name == TfToken("blue")) {
		return BaseMaterial3D::TEXTURE_CHANNEL_BLUE;
	}
	if (p_output_name == TfToken("a") || p_output_name == TfToken("alpha")) {
		return BaseMaterial3D::TEXTURE_CHANNEL_ALPHA;
	}
	return BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE;
}

void record_preview_texture_source(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, Dictionary *r_texture_sources, const String &p_input_name, const UsdShaderConnection &p_connection) {
	ERR_FAIL_NULL(r_texture_sources);
	if (p_stage == nullptr || !p_connection) {
		return;
	}

	UsdShadeInput file_input = p_connection.shader.GetInput(TfToken("file"));
	if (!file_input) {
		return;
	}

	SdfAssetPath asset_path;
	if (!file_input.Get(&asset_path, p_time)) {
		return;
	}

	const String authored_path = normalize_usd_asset_path(to_godot_string(asset_path.GetAssetPath()));
	const String resolved_path = resolve_asset_path(p_stage, asset_path);
	if (authored_path.is_empty() && resolved_path.is_empty()) {
		return;
	}

	Dictionary source_description;
	source_description["asset_path"] = authored_path.is_empty() ? resolved_path : authored_path;
	source_description["output_name"] = to_godot_string(p_connection.output_name.GetString());
	source_description["shader_path"] = to_godot_string(p_connection.shader.GetPath().GetString());
	(*r_texture_sources)[p_input_name] = source_description;
}

Ref<Image> make_opacity_composited_albedo(const Ref<Texture2D> &p_albedo_texture, const Ref<Image> &p_opacity_image, BaseMaterial3D::TextureChannel p_opacity_channel) {
	if (p_opacity_image.is_null()) {
		return Ref<Image>();
	}

	Ref<Image> opacity_image = p_opacity_image->duplicate(true);
	if (opacity_image.is_null()) {
		return Ref<Image>();
	}
	if (opacity_image->is_compressed()) {
		opacity_image->decompress();
	}
	opacity_image->convert(Image::FORMAT_RGBA8);

	Ref<Image> albedo_image;
	if (p_albedo_texture.is_valid()) {
		albedo_image = p_albedo_texture->get_image();
	}
	if (albedo_image.is_valid()) {
		albedo_image = albedo_image->duplicate(true);
		if (albedo_image->is_compressed()) {
			albedo_image->decompress();
		}
		albedo_image->convert(Image::FORMAT_RGBA8);
	} else {
		albedo_image = Image::create_empty(opacity_image->get_width(), opacity_image->get_height(), false, Image::FORMAT_RGBA8);
		albedo_image->fill(Color(1.0f, 1.0f, 1.0f, 1.0f));
	}

	if (opacity_image->get_width() != albedo_image->get_width() || opacity_image->get_height() != albedo_image->get_height()) {
		opacity_image->resize(albedo_image->get_width(), albedo_image->get_height(), Image::INTERPOLATE_BILINEAR);
	}

	for (int y = 0; y < albedo_image->get_height(); y++) {
		for (int x = 0; x < albedo_image->get_width(); x++) {
			Color albedo = albedo_image->get_pixel(x, y);
			const Color opacity = opacity_image->get_pixel(x, y);
			switch (p_opacity_channel) {
				case BaseMaterial3D::TEXTURE_CHANNEL_RED:
					albedo.a = opacity.r;
					break;
				case BaseMaterial3D::TEXTURE_CHANNEL_GREEN:
					albedo.a = opacity.g;
					break;
				case BaseMaterial3D::TEXTURE_CHANNEL_BLUE:
					albedo.a = opacity.b;
					break;
				case BaseMaterial3D::TEXTURE_CHANNEL_ALPHA:
					albedo.a = opacity.a;
					break;
				case BaseMaterial3D::TEXTURE_CHANNEL_GRAYSCALE:
				default:
					albedo.a = opacity.get_luminance();
					break;
			}
			albedo_image->set_pixel(x, y, albedo);
		}
	}

	return albedo_image;
}

float preview_surface_f0_from_ior(float p_ior) {
	if (p_ior <= 0.0f) {
		return 0.04f;
	}

	const float reflectance = (p_ior - 1.0f) / (p_ior + 1.0f);
	return reflectance * reflectance;
}

float godot_specular_from_preview_f0(float p_f0) {
	return CLAMP(Math::sqrt(MAX(p_f0, 0.0f) / 0.16f), 0.0f, 1.0f);
}

Ref<Material> build_material_from_usd_material(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdShadeMaterial &p_material, Dictionary *r_mapping_notes) {
	if (p_stage == nullptr || !p_material) {
		return Ref<Material>();
	}

	const String material_path = to_godot_string(p_material.GetPath().GetString());
	UsdShadeShader preview_surface = p_material.ComputeSurfaceSource();
	if (preview_surface) {
		preview_surface = UsdShadeShader(p_stage->GetPrimAtPath(preview_surface.GetPath()));
	}
	if (!preview_surface) {
		if (r_mapping_notes != nullptr) {
			(*r_mapping_notes)["usd:material_status"] = vformat("Material %s has no supported surface source.", material_path);
		}
		return Ref<Material>();
	}

	TfToken shader_id;
	preview_surface.GetPrim().GetAttribute(TfToken("info:id")).Get(&shader_id, p_time);
	if (shader_id != TfToken("UsdPreviewSurface")) {
		if (r_mapping_notes != nullptr) {
			(*r_mapping_notes)["usd:material_status"] = vformat("Material %s uses unsupported shader id %s.", material_path, to_godot_string(shader_id.GetString()));
		}
		return Ref<Material>();
	}

	Ref<StandardMaterial3D> material;
	material.instantiate();
	bool has_uv_transform = false;
	Vector3 uv_scale;
	Vector3 uv_offset;
	Dictionary texture_sources;

	UsdShadeInput diffuse_input = preview_surface.GetInput(TfToken("diffuseColor"));
	if (diffuse_input) {
		GfVec3f diffuse_color(1.0f, 1.0f, 1.0f);
		diffuse_input.Get(&diffuse_color, p_time);
		material->set_albedo(Color(diffuse_color[0], diffuse_color[1], diffuse_color[2], 1.0f));

		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, diffuse_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "diffuseColor", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture);
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	float metallic = 0.0f;
	UsdShadeInput metallic_input = preview_surface.GetInput(TfToken("metallic"));
	if (metallic_input && metallic_input.Get(&metallic, p_time)) {
		material->set_metallic(metallic);
	}
	if (metallic_input && metallic_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, metallic_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "metallic", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_texture(BaseMaterial3D::TEXTURE_METALLIC, texture);
				material->set_metallic_texture_channel(get_texture_channel_for_output(texture_connection.output_name));
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	float roughness = 0.5f;
	UsdShadeInput roughness_input = preview_surface.GetInput(TfToken("roughness"));
	if (roughness_input && roughness_input.Get(&roughness, p_time)) {
		material->set_roughness(roughness);
	}
	if (roughness_input && roughness_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, roughness_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "roughness", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_texture(BaseMaterial3D::TEXTURE_ROUGHNESS, texture);
				material->set_roughness_texture_channel(get_texture_channel_for_output(texture_connection.output_name));
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	UsdShadeInput normal_input = preview_surface.GetInput(TfToken("normal"));
	if (normal_input && normal_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, normal_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "normal", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_feature(BaseMaterial3D::FEATURE_NORMAL_MAPPING, true);
				material->set_texture(BaseMaterial3D::TEXTURE_NORMAL, texture);
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	bool has_opacity = false;
	float opacity = 1.0f;
	UsdShadeInput opacity_input = preview_surface.GetInput(TfToken("opacity"));
	if (opacity_input && opacity_input.Get(&opacity, p_time)) {
		if (opacity < 1.0f) {
			Color albedo = material->get_albedo();
			albedo.a = opacity;
			material->set_albedo(albedo);
			material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			has_opacity = true;
		}
	}
	if (opacity_input && opacity_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, opacity_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "opacity", texture_connection);
			Ref<Image> opacity_image = load_image_from_shader(p_stage, p_time, texture_connection.shader);
			if (opacity_image.is_valid()) {
				const Ref<Image> composited_albedo = make_opacity_composited_albedo(material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO), opacity_image, get_texture_channel_for_output(texture_connection.output_name));
				if (composited_albedo.is_valid()) {
					material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, texture_from_image(composited_albedo));
					material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
					has_opacity = true;
				}
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	float opacity_threshold = 0.0f;
	UsdShadeInput opacity_threshold_input = preview_surface.GetInput(TfToken("opacityThreshold"));
	if (opacity_threshold_input && opacity_threshold_input.Get(&opacity_threshold, p_time) && opacity_threshold > 0.0f) {
		material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA_SCISSOR);
		material->set_alpha_scissor_threshold(CLAMP(opacity_threshold, 0.0f, 1.0f));
	} else if (has_opacity) {
		material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	}

	GfVec3f emission(0.0f, 0.0f, 0.0f);
	UsdShadeInput emission_input = preview_surface.GetInput(TfToken("emissiveColor"));
	bool has_emission = false;
	if (emission_input && emission_input.Get(&emission, p_time)) {
		const Color emission_color(emission[0], emission[1], emission[2], 1.0f);
		if (emission_color.r > 0.0f || emission_color.g > 0.0f || emission_color.b > 0.0f) {
			material->set_emission(emission_color);
			has_emission = true;
		}
	}
	if (emission_input && emission_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, emission_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "emissiveColor", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_texture(BaseMaterial3D::TEXTURE_EMISSION, texture);
				has_emission = true;
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}
	if (has_emission) {
		material->set_feature(BaseMaterial3D::FEATURE_EMISSION, true);
		material->set_emission_energy_multiplier(1.0f);
	}

	bool use_specular_workflow = false;
	if (UsdShadeInput use_specular_workflow_input = preview_surface.GetInput(TfToken("useSpecularWorkflow"))) {
		use_specular_workflow_input.Get(&use_specular_workflow, p_time);
	}
	if (use_specular_workflow) {
		set_usd_metadata(material.ptr(), "usd:preview_surface_use_specular_workflow", true);
	}

	bool has_ior = false;
	float ior = 1.5f;
	if (UsdShadeInput ior_input = preview_surface.GetInput(TfToken("ior"))) {
		has_ior = ior_input.Get(&ior, p_time);
		if (has_ior) {
			set_usd_metadata(material.ptr(), "usd:preview_surface_ior", ior);
		}
	}

	UsdShadeInput specular_input = preview_surface.GetInput(TfToken("specularColor"));
	bool applied_specular_override = false;
	if (use_specular_workflow && specular_input) {
		GfVec3f specular_color(0.04f, 0.04f, 0.04f);
		if (specular_input.Get(&specular_color, p_time)) {
			const Color specular_preview_color(specular_color[0], specular_color[1], specular_color[2], 1.0f);
			set_usd_metadata(material.ptr(), "usd:preview_surface_specular_color", specular_preview_color);
			material->set_specular(godot_specular_from_preview_f0(specular_preview_color.get_luminance()));
			applied_specular_override = true;
		}

		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, specular_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "specularColor", texture_connection);
		}
	}
	if (!applied_specular_override && has_ior) {
		material->set_specular(godot_specular_from_preview_f0(preview_surface_f0_from_ior(ior)));
	}

	float clearcoat = 0.0f;
	if (UsdShadeInput clearcoat_input = preview_surface.GetInput(TfToken("clearcoat"))) {
		if (clearcoat_input.Get(&clearcoat, p_time)) {
			material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
			material->set_clearcoat(clearcoat);
		}
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, clearcoat_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "clearcoat", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
				material->set_texture(BaseMaterial3D::TEXTURE_CLEARCOAT, texture);
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	float clearcoat_roughness = 0.01f;
	if (UsdShadeInput clearcoat_roughness_input = preview_surface.GetInput(TfToken("clearcoatRoughness"))) {
		if (clearcoat_roughness_input.Get(&clearcoat_roughness, p_time)) {
			material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
			material->set_clearcoat_roughness(clearcoat_roughness);
		}
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, clearcoat_roughness_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "clearcoatRoughness", texture_connection);
		}
	}

	UsdShadeInput occlusion_input = preview_surface.GetInput(TfToken("occlusion"));
	if (occlusion_input && occlusion_input.HasConnectedSource()) {
		const UsdShaderConnection texture_connection = get_connected_texture_shader(p_stage, p_time, occlusion_input);
		if (texture_connection) {
			record_preview_texture_source(p_stage, p_time, &texture_sources, "occlusion", texture_connection);
			Ref<Texture2D> texture = load_texture_from_shader(p_stage, p_time, texture_connection.shader);
			if (texture.is_valid()) {
				material->set_feature(BaseMaterial3D::FEATURE_AMBIENT_OCCLUSION, true);
				material->set_texture(BaseMaterial3D::TEXTURE_AMBIENT_OCCLUSION, texture);
				material->set_ao_texture_channel(get_texture_channel_for_output(texture_connection.output_name));
				merge_material_uv_transform(p_stage, p_time, texture_connection.shader, material.ptr(), &has_uv_transform, &uv_scale, &uv_offset, r_mapping_notes);
			}
		}
	}

	if (!texture_sources.is_empty()) {
		set_usd_metadata(material.ptr(), "usd:preview_surface_texture_sources", texture_sources);
	}

	return material;
}

Ref<Material> make_display_color_material(const Color &p_color, bool p_use_vertex_colors) {
	Ref<StandardMaterial3D> material;
	material.instantiate();
	material->set_albedo(p_color);
	if (p_use_vertex_colors) {
		material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		material->set_flag(BaseMaterial3D::FLAG_SRGB_VERTEX_COLOR, true);
	}
	return material;
}

} // namespace godot_usd
