#include "usd_editor_plugin.h"

#include <godot_cpp/classes/global_constants.hpp>

using namespace godot;

void UsdEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		if (scene_importer.is_null()) {
			scene_importer.instantiate();
			add_scene_format_importer_plugin(scene_importer, true);
		}
		return;
	}

	if (p_what == NOTIFICATION_EXIT_TREE) {
		if (scene_importer.is_valid()) {
			remove_scene_format_importer_plugin(scene_importer);
			scene_importer.unref();
		}
	}
}

String UsdEditorPlugin::_get_plugin_name() const {
	return "Godot USD";
}

bool UsdEditorPlugin::_has_main_screen() const {
	return false;
}
