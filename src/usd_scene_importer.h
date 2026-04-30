#pragma once

#include <godot_cpp/classes/editor_scene_format_importer.hpp>

using namespace godot;

class UsdSceneFormatImporter : public EditorSceneFormatImporter {
	GDCLASS(UsdSceneFormatImporter, EditorSceneFormatImporter);

protected:
	static void _bind_methods() {}

public:
	PackedStringArray _get_extensions() const override;
	Object *_import_scene(const String &p_path, uint32_t p_flags, const Dictionary &p_options) override;
	void _get_import_options(const String &p_path) override;
	Variant _get_option_visibility(const String &p_path, bool p_for_animation, const String &p_option) const override;
};
