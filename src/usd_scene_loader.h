#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/resource_format_loader.hpp>
#include <godot_cpp/classes/resource_format_saver.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/templates/list.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/variant.hpp>

#include "usd_light_proxy.h"

using namespace godot;

class UsdStageResource : public Resource {
	GDCLASS(UsdStageResource, Resource);

	String source_path;
	Dictionary stage_metadata;
	Dictionary variant_sets;

protected:
	static void _bind_methods();

public:
	void set_source_path(const String &p_source_path);
	String get_source_path() const;
	Dictionary get_stage_metadata() const;
	Dictionary get_variant_sets() const;
	Error refresh_metadata();
};

class UsdStageInstance : public Node3D {
	GDCLASS(UsdStageInstance, Node3D);

	Ref<UsdStageResource> stage;
	Dictionary variant_selections;
	Dictionary composed_variant_sets;
	Dictionary generated_node_baselines;
	Dictionary runtime_node_overrides;
	Node *generated_root = nullptr;
	bool debug_logging = false;
	bool rebuilt_after_scene_instantiation = false;
	bool skip_next_runtime_override_capture = false;
	int debug_rebuild_count = 0;
	String debug_last_selection_change;
	String debug_last_rebuild_status;
	String debug_last_generated_summary;

	void _clear_node_children(Node *p_node);
	void _clear_generated_children();
	bool _is_generated_root(Node *p_node) const;
	void _adopt_existing_generated_root();
	Node *_get_generated_owner() const;
	void _mark_generated_tree_owned();
	Node *_find_node_for_prim_path(Node *p_node, const String &p_prim_path) const;
	void _append_generated_summary(Node *p_node, PackedStringArray *r_summary, int p_limit) const;
	String _get_generated_summary() const;
	String _get_prim_path_for_node(const Node *p_node) const;
	bool _get_node3d_runtime_state(Node *p_node, Dictionary *r_state) const;
	bool _node3d_runtime_state_matches(Node *p_node, const Dictionary &p_state) const;
	void _collect_runtime_node_baselines(Node *p_node, Dictionary *r_baselines) const;
	void _capture_runtime_node_overrides();
	void _refresh_runtime_node_baselines();
	void _apply_runtime_node_overrides(Node *p_node);
	bool _parse_variant_property(const String &p_property, String *r_prim_path, String *r_variant_set) const;
	String _get_variant_selection(const String &p_prim_path, const String &p_variant_set) const;
	void _set_variant_selection_property(const String &p_prim_path, const String &p_variant_set, const String &p_selection);
	void _stage_changed();

protected:
	static void _bind_methods();
	void _notification(int p_what);
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

public:
	~UsdStageInstance();
	void set_stage(const Ref<UsdStageResource> &p_stage);
	Ref<UsdStageResource> get_stage() const;
	void set_variant_selections(const Dictionary &p_variant_selections);
	Dictionary get_variant_selections() const;
	void set_runtime_node_overrides(const Dictionary &p_runtime_node_overrides);
	Dictionary get_runtime_node_overrides() const;
	void set_debug_logging(bool p_debug_logging);
	bool is_debug_logging() const;
	int get_debug_rebuild_count() const;
	String get_debug_last_selection_change() const;
	String get_debug_last_rebuild_status() const;
	String get_debug_last_generated_summary() const;
	Error rebuild();
	Node *get_node_for_prim_path(const String &p_prim_path) const;
};

class UsdSceneFormatLoader : public ResourceFormatLoader {
	GDCLASS(UsdSceneFormatLoader, ResourceFormatLoader);

protected:
	static void _bind_methods() {}

public:
	PackedStringArray _get_recognized_extensions() const override;
	bool _recognize_path(const String &p_path, const StringName &p_type) const override;
	bool _handles_type(const StringName &p_type) const override;
	String _get_resource_type(const String &p_path) const override;
	bool _exists(const String &p_path) const override;
	Variant _load(const String &p_path, const String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
};

class UsdSceneFormatSaver : public ResourceFormatSaver {
	GDCLASS(UsdSceneFormatSaver, ResourceFormatSaver);

protected:
	static void _bind_methods() {}

public:
	Error _save(const Ref<Resource> &p_resource, const String &p_path, uint32_t p_flags) override;
	bool _recognize(const Ref<Resource> &p_resource) const override;
	PackedStringArray _get_recognized_extensions(const Ref<Resource> &p_resource) const override;
	bool _recognize_path(const Ref<Resource> &p_resource, const String &p_path) const override;
};
