#include "usd_stage_utils.h"

#include <algorithm>
#include <vector>

#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <pxr/base/tf/token.h>
#include <pxr/usd/sdf/layer.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/variantSets.h>
#include <pxr/usd/usdGeom/metrics.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

namespace {

constexpr const char *USD_META_KEY = "usd";

struct VariantSelectionRequest {
	String prim_path;
	String variant_set;
	String selection;
	int path_depth = 0;
};

int get_prim_path_depth(const String &p_prim_path) {
	int depth = 0;
	for (int i = 0; i < p_prim_path.length(); i++) {
		if (p_prim_path[i] == '/') {
			depth++;
		}
	}
	return depth;
}

void add_variant_selection_request(std::vector<VariantSelectionRequest> &r_requests, const String &p_prim_path, const String &p_variant_set_name, const String &p_selection) {
	if (p_prim_path.is_empty() || p_variant_set_name.is_empty() || p_selection.is_empty()) {
		return;
	}

	VariantSelectionRequest request;
	request.prim_path = p_prim_path;
	request.variant_set = p_variant_set_name;
	request.selection = p_selection;
	request.path_depth = get_prim_path_depth(p_prim_path);
	r_requests.push_back(request);
}

bool set_variant_selection(const UsdStageRefPtr &p_stage, const String &p_prim_path, const String &p_variant_set_name, const String &p_selection) {
	ERR_FAIL_COND_V(p_stage == nullptr, false);
	UsdPrim prim = p_stage->GetPrimAtPath(SdfPath(p_prim_path.utf8().get_data()));
	if (!prim) {
		return false;
	}

	UsdVariantSet variant_set = prim.GetVariantSets().GetVariantSet(TfToken(p_variant_set_name.utf8().get_data()));
	if (!variant_set) {
		return false;
	}

	return variant_set.SetVariantSelection(p_selection.utf8().get_data());
}

void apply_variant_selections(const UsdStageRefPtr &p_stage, const Dictionary &p_variant_selections) {
	std::vector<VariantSelectionRequest> requests;
	Array prim_keys = p_variant_selections.keys();
	for (int i = 0; i < prim_keys.size(); i++) {
		const String prim_path = prim_keys[i];
		const Variant prim_value = p_variant_selections[prim_path];
		if (prim_value.get_type() == Variant::DICTIONARY) {
			const Dictionary prim_selections = prim_value;
			Array set_keys = prim_selections.keys();
			for (int set_index = 0; set_index < set_keys.size(); set_index++) {
				const String set_name = set_keys[set_index];
				const Variant selection = prim_selections[set_name];
				if (selection.get_type() == Variant::STRING || selection.get_type() == Variant::STRING_NAME) {
					add_variant_selection_request(requests, prim_path, set_name, selection);
				}
			}
			continue;
		}

		if (prim_value.get_type() != Variant::STRING && prim_value.get_type() != Variant::STRING_NAME) {
			continue;
		}

		const int separator = prim_path.rfind(":");
		if (separator <= 0) {
			continue;
		}
		add_variant_selection_request(requests, prim_path.substr(0, separator), prim_path.substr(separator + 1), prim_value);
	}

	std::sort(requests.begin(), requests.end(), [](const VariantSelectionRequest &p_left, const VariantSelectionRequest &p_right) {
		if (p_left.path_depth != p_right.path_depth) {
			return p_left.path_depth < p_right.path_depth;
		}
		return p_left.prim_path < p_right.prim_path;
	});

	for (const VariantSelectionRequest &request : requests) {
		set_variant_selection(p_stage, request.prim_path, request.variant_set, request.selection);
	}
}

} // namespace

String to_godot_string(const std::string &p_string) {
	return String::utf8(p_string.c_str());
}

String get_absolute_path(const String &p_path) {
	if (p_path.begins_with("res://") || p_path.begins_with("user://")) {
		return ProjectSettings::get_singleton()->globalize_path(p_path);
	}
	return p_path;
}

bool is_usd_scene_extension(const String &p_extension) {
	const String extension = p_extension.to_lower();
	return extension == "usd" || extension == "usda" || extension == "usdc" || extension == "usdz";
}

Transform3D gf_matrix_to_transform(const GfMatrix4d &p_matrix) {
	Basis basis(
			(real_t)p_matrix[0][0], (real_t)p_matrix[1][0], (real_t)p_matrix[2][0],
			(real_t)p_matrix[0][1], (real_t)p_matrix[1][1], (real_t)p_matrix[2][1],
			(real_t)p_matrix[0][2], (real_t)p_matrix[1][2], (real_t)p_matrix[2][2]);
	Vector3 origin((real_t)p_matrix[3][0], (real_t)p_matrix[3][1], (real_t)p_matrix[3][2]);
	return Transform3D(basis, origin);
}

Dictionary get_usd_metadata(const Object *p_object) {
	if (p_object == nullptr || !p_object->has_meta(StringName(USD_META_KEY))) {
		return Dictionary();
	}
	const Variant metadata = p_object->get_meta(StringName(USD_META_KEY), Dictionary());
	if (metadata.get_type() == Variant::DICTIONARY) {
		return metadata;
	}
	return Dictionary();
}

void set_usd_metadata(Object *p_object, const String &p_name, const Variant &p_value) {
	Dictionary metadata = get_usd_metadata(p_object);
	metadata[p_name] = p_value;
	p_object->set_meta(StringName(USD_META_KEY), metadata);
}

void set_usd_metadata_entries(Object *p_object, const Dictionary &p_entries) {
	Dictionary metadata = get_usd_metadata(p_object);
	Array keys = p_entries.keys();
	for (int i = 0; i < keys.size(); i++) {
		metadata[keys[i]] = p_entries[keys[i]];
	}
	p_object->set_meta(StringName(USD_META_KEY), metadata);
}

void mark_owner_recursive(Node *p_node, Node *p_owner) {
	if (p_node != p_owner) {
		p_node->set_owner(p_owner);
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		mark_owner_recursive(p_node->get_child(i), p_owner);
	}
}

Dictionary collect_stage_metadata(const UsdStageRefPtr &p_stage) {
	Dictionary metadata;
	ERR_FAIL_COND_V(p_stage == nullptr, metadata);

	metadata["usd:source_identifier"] = to_godot_string(p_stage->GetRootLayer()->GetIdentifier());
	metadata["usd:up_axis"] = to_godot_string(UsdGeomGetStageUpAxis(p_stage).GetString());
	metadata["usd:meters_per_unit"] = UsdGeomGetStageMetersPerUnit(p_stage);
	if (UsdPrim default_prim = p_stage->GetDefaultPrim()) {
		metadata["usd:default_prim_path"] = to_godot_string(default_prim.GetPath().GetString());
	}

	return metadata;
}

Dictionary collect_variant_sets(const UsdStageRefPtr &p_stage) {
	Dictionary variant_catalog;
	ERR_FAIL_COND_V(p_stage == nullptr, variant_catalog);

	for (const UsdPrim &prim : p_stage->TraverseAll()) {
		UsdVariantSets variant_sets = prim.GetVariantSets();
		std::vector<std::string> set_names;
		variant_sets.GetNames(&set_names);
		if (set_names.empty()) {
			continue;
		}

		Dictionary prim_variant_sets;
		for (const std::string &set_name : set_names) {
			UsdVariantSet variant_set = variant_sets.GetVariantSet(set_name);
			if (!variant_set) {
				continue;
			}

			Array variants;
			const std::vector<std::string> variant_names = variant_set.GetVariantNames();
			for (const std::string &variant_name : variant_names) {
				variants.push_back(to_godot_string(variant_name));
			}

			Dictionary set_description;
			set_description["prim_path"] = to_godot_string(prim.GetPath().GetString());
			set_description["variant_set"] = to_godot_string(set_name);
			set_description["variants"] = variants;
			set_description["selection"] = to_godot_string(variant_set.GetVariantSelection());
			prim_variant_sets[to_godot_string(set_name)] = set_description;
		}

		if (!prim_variant_sets.is_empty()) {
			variant_catalog[to_godot_string(prim.GetPath().GetString())] = prim_variant_sets;
		}
	}

	return variant_catalog;
}

UsdStageRefPtr open_stage_for_instance(const String &p_source_path, const Dictionary &p_variant_selections) {
	const String absolute_path = get_absolute_path(p_source_path);
	UsdStageRefPtr direct_stage = UsdStage::Open(absolute_path.utf8().get_data(), UsdStage::LoadAll);
	ERR_FAIL_COND_V_MSG(!direct_stage, nullptr, vformat("Failed to open USD stage: %s", p_source_path));

	if (p_variant_selections.is_empty()) {
		return direct_stage;
	}

	SdfLayerRefPtr root_layer = direct_stage->GetRootLayer();
	ERR_FAIL_COND_V_MSG(!root_layer, nullptr, vformat("Failed to access USD root layer: %s", p_source_path));

	SdfLayerRefPtr session_layer = SdfLayer::CreateAnonymous("GodotUsdStageInstanceSession.usda");
	UsdStageRefPtr stage = UsdStage::Open(root_layer, session_layer, UsdStage::LoadAll);
	ERR_FAIL_COND_V_MSG(!stage, nullptr, vformat("Failed to compose USD stage: %s", p_source_path));

	stage->SetEditTarget(session_layer);
	apply_variant_selections(stage, p_variant_selections);
	stage = UsdStage::Open(root_layer, session_layer, UsdStage::LoadAll);
	ERR_FAIL_COND_V_MSG(!stage, nullptr, vformat("Failed to recompose USD stage with variant selections: %s", p_source_path));
	return stage;
}

} // namespace godot_usd
