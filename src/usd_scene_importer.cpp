#include "usd_scene_importer.h"

#include <godot_cpp/classes/json.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/packed_scene.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/core/memory.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include "usd_scene_loader.h"

using namespace godot;

namespace {

constexpr const char *USD_IMPORT_OPTION_VARIANT_SELECTIONS = "usd/variant_selections";
constexpr const char *USD_IMPORT_OPTION_VARIANT_PREFIX = "usd/variants/";
constexpr const char *USD_STAGE_INSTANCE_GENERATED_META = "usd_stage_instance_generated";

String get_variant_option_name(const String &p_prim_path, const String &p_variant_set_name) {
	String property_prim_path = p_prim_path.trim_prefix("/");
	if (property_prim_path.is_empty()) {
		property_prim_path = "_root";
	}
	return String(USD_IMPORT_OPTION_VARIANT_PREFIX) + property_prim_path + "/" + p_variant_set_name;
}

bool parse_variant_option_name(const String &p_option, String *r_prim_path, String *r_variant_set_name) {
	ERR_FAIL_NULL_V(r_prim_path, false);
	ERR_FAIL_NULL_V(r_variant_set_name, false);
	if (!p_option.begins_with(USD_IMPORT_OPTION_VARIANT_PREFIX)) {
		return false;
	}

	String suffix = p_option.trim_prefix(USD_IMPORT_OPTION_VARIANT_PREFIX);
	const int separator = suffix.rfind("/");
	if (separator <= 0 || separator == suffix.length() - 1) {
		return false;
	}

	const String property_prim_path = suffix.substr(0, separator);
	*r_prim_path = property_prim_path == "_root" ? "/" : "/" + property_prim_path;
	*r_variant_set_name = suffix.substr(separator + 1);
	return !r_prim_path->is_empty() && !r_variant_set_name->is_empty();
}

Dictionary merge_variant_selection_dicts(const Dictionary &p_base, const Dictionary &p_override) {
	Dictionary merged = p_base.duplicate(true);
	Array prim_keys = p_override.keys();
	for (int i = 0; i < prim_keys.size(); i++) {
		const Variant prim_key = prim_keys[i];
		const Variant override_value = p_override[prim_key];
		if (override_value.get_type() != Variant::DICTIONARY) {
			continue;
		}

		Dictionary merged_prim;
		const Variant base_value = merged.get(prim_key, Variant());
		if (base_value.get_type() == Variant::DICTIONARY) {
			merged_prim = base_value;
		}

		const Dictionary override_prim = override_value;
		Array set_keys = override_prim.keys();
		for (int set_index = 0; set_index < set_keys.size(); set_index++) {
			const Variant set_key = set_keys[set_index];
			const Variant set_value = override_prim[set_key];
			if (set_value.get_type() != Variant::STRING && set_value.get_type() != Variant::STRING_NAME) {
				continue;
			}
			merged_prim[set_key] = set_value;
		}

		if (!merged_prim.is_empty()) {
			merged[prim_key] = merged_prim;
		}
	}
	return merged;
}

Dictionary collect_variant_selections_from_structured_options(const Dictionary &p_options) {
	Dictionary variant_selections;
	Array keys = p_options.keys();
	for (int i = 0; i < keys.size(); i++) {
		const String option_name = keys[i];
		String prim_path;
		String variant_set_name;
		if (!parse_variant_option_name(option_name, &prim_path, &variant_set_name)) {
			continue;
		}

		const Variant option_value = p_options[option_name];
		if (option_value.get_type() != Variant::STRING && option_value.get_type() != Variant::STRING_NAME) {
			continue;
		}

		const String selection = String(option_value).strip_edges();
		if (selection.is_empty()) {
			continue;
		}

		Dictionary prim_selections;
		const Variant existing = variant_selections.get(prim_path, Variant());
		if (existing.get_type() == Variant::DICTIONARY) {
			prim_selections = existing;
		}
		prim_selections[variant_set_name] = selection;
		variant_selections[prim_path] = prim_selections;
	}
	return variant_selections;
}

bool parse_import_variant_selections(const Dictionary &p_options, Dictionary *r_variant_selections) {
	ERR_FAIL_NULL_V(r_variant_selections, false);
	r_variant_selections->clear();

	const Variant serialized_variant = p_options.get(String(USD_IMPORT_OPTION_VARIANT_SELECTIONS), Variant());
	if (serialized_variant.get_type() != Variant::STRING && serialized_variant.get_type() != Variant::STRING_NAME) {
		return true;
	}

	const String serialized = String(serialized_variant).strip_edges();
	if (serialized.is_empty()) {
		return true;
	}

	const Variant parsed = JSON::parse_string(serialized);
	if (parsed.get_type() != Variant::DICTIONARY) {
		UtilityFunctions::push_error("UsdSceneFormatImporter expected usd/variant_selections to be a JSON object.");
		return false;
	}

	*r_variant_selections = parsed;
	return true;
}

void clear_generated_root_marker_recursive(Node *p_node) {
	ERR_FAIL_NULL(p_node);
	if (p_node->has_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META))) {
		p_node->remove_meta(StringName(USD_STAGE_INSTANCE_GENERATED_META));
	}
	for (int i = 0; i < p_node->get_child_count(); i++) {
		clear_generated_root_marker_recursive(p_node->get_child(i));
	}
}

Dictionary get_stage_variant_catalog(const String &p_path) {
	Ref<UsdStageResource> stage;
	stage.instantiate();
	stage->set_source_path(p_path);
	return stage->get_variant_sets();
}

} // namespace

PackedStringArray UsdSceneFormatImporter::_get_extensions() const {
	PackedStringArray extensions;
	extensions.push_back("usd");
	extensions.push_back("usda");
	extensions.push_back("usdc");
	extensions.push_back("usdz");
	return extensions;
}

Object *UsdSceneFormatImporter::_import_scene(const String &p_path, uint32_t p_flags, const Dictionary &p_options) {
	(void)p_flags;

	Dictionary variant_selections;
	if (!parse_import_variant_selections(p_options, &variant_selections)) {
		return nullptr;
	}
	variant_selections = merge_variant_selection_dicts(variant_selections, collect_variant_selections_from_structured_options(p_options));

	Ref<Resource> loaded = ResourceLoader::get_singleton()->load(p_path, "PackedScene", ResourceLoader::CACHE_MODE_IGNORE);
	Ref<PackedScene> packed_scene = loaded;
	if (packed_scene.is_null()) {
		return nullptr;
	}

	Node *root = packed_scene->instantiate();
	ERR_FAIL_NULL_V(root, nullptr);

	UsdStageInstance *stage_instance = Object::cast_to<UsdStageInstance>(root);
	if (stage_instance == nullptr) {
		return root;
	}

	if (!variant_selections.is_empty()) {
		stage_instance->set_variant_selections(variant_selections);
	}
	if (stage_instance->rebuild() != OK) {
		memdelete(stage_instance);
		return nullptr;
	}

	Node *generated_root = stage_instance->get_node_or_null(NodePath("_Generated"));
	if (generated_root == nullptr) {
		memdelete(stage_instance);
		return nullptr;
	}

	stage_instance->remove_child(generated_root);
	clear_generated_root_marker_recursive(generated_root);
	generated_root->set_name(p_path.get_file().get_basename());
	memdelete(stage_instance);
	return generated_root;
}

void UsdSceneFormatImporter::_get_import_options(const String &p_path) {
	add_import_option_advanced(Variant::STRING, USD_IMPORT_OPTION_VARIANT_SELECTIONS, String(), PROPERTY_HINT_NONE, String(), PROPERTY_USAGE_NO_EDITOR);
	if (p_path.is_empty()) {
		return;
	}

	const Dictionary variant_catalog = get_stage_variant_catalog(p_path);
	Array prim_keys = variant_catalog.keys();
	for (int i = 0; i < prim_keys.size(); i++) {
		const String prim_path = prim_keys[i];
		const Variant prim_sets_variant = variant_catalog[prim_path];
		if (prim_sets_variant.get_type() != Variant::DICTIONARY) {
			continue;
		}

		const Dictionary prim_sets = prim_sets_variant;
		Array set_keys = prim_sets.keys();
		for (int set_index = 0; set_index < set_keys.size(); set_index++) {
			const String variant_set_name = set_keys[set_index];
			const Variant set_description_variant = prim_sets[variant_set_name];
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

			add_import_option_advanced(
					Variant::STRING,
					get_variant_option_name(prim_path, variant_set_name),
					String(set_description.get("selection", String())),
					PROPERTY_HINT_ENUM,
					hint_string,
					PROPERTY_USAGE_DEFAULT | PROPERTY_USAGE_UPDATE_ALL_IF_MODIFIED);
		}
	}
}

Variant UsdSceneFormatImporter::_get_option_visibility(const String &p_path, bool p_for_animation, const String &p_option) const {
	(void)p_path;
	(void)p_for_animation;
	if (p_option == String(USD_IMPORT_OPTION_VARIANT_SELECTIONS)) {
		return false;
	}
	return true;
}
