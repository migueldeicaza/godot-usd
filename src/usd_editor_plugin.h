#pragma once

#include <godot_cpp/classes/editor_plugin.hpp>
#include <godot_cpp/classes/ref.hpp>

#include "usd_scene_importer.h"

using namespace godot;

class UsdEditorPlugin : public EditorPlugin {
	GDCLASS(UsdEditorPlugin, EditorPlugin);

	Ref<UsdSceneFormatImporter> scene_importer;

protected:
	static void _bind_methods() {}
	void _notification(int p_what);

public:
	String _get_plugin_name() const override;
	bool _has_main_screen() const override;
};
