#include "register_types.h"

#include <gdextension_interface.h>

#include <godot_cpp/classes/editor_plugin_registration.hpp>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/classes/resource_saver.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include "usd_editor_plugin.h"
#include "usd_scene_importer.h"
#include "usd_scene_loader.h"

using namespace godot;

static Ref<UsdSceneFormatLoader> s_usd_loader;
static Ref<UsdSceneFormatSaver> s_usd_saver;

void initialize_godot_usd_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(UsdStageResource);
		GDREGISTER_CLASS(UsdStageInstance);
		GDREGISTER_CLASS(UsdSceneFormatLoader);
		GDREGISTER_CLASS(UsdSceneFormatSaver);

		s_usd_loader.instantiate();
		s_usd_saver.instantiate();

		ResourceLoader::get_singleton()->add_resource_format_loader(s_usd_loader, true);
		ResourceSaver::get_singleton()->add_resource_format_saver(s_usd_saver, true);
		return;
	}

	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		GDREGISTER_CLASS(UsdSceneFormatImporter);
		GDREGISTER_CLASS(UsdEditorPlugin);
		EditorPlugins::add_by_type<UsdEditorPlugin>();
	}
}

void uninitialize_godot_usd_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		if (s_usd_loader.is_valid()) {
			ResourceLoader::get_singleton()->remove_resource_format_loader(s_usd_loader);
			s_usd_loader.unref();
		}
		if (s_usd_saver.is_valid()) {
			ResourceSaver::get_singleton()->remove_resource_format_saver(s_usd_saver);
			s_usd_saver.unref();
		}
	}
}

extern "C" {
GDExtensionBool GDE_EXPORT godot_usd_library_init(
		GDExtensionInterfaceGetProcAddress p_get_proc_address,
		GDExtensionClassLibraryPtr p_library,
		GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_godot_usd_module);
	init_obj.register_terminator(uninitialize_godot_usd_module);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);
	return init_obj.init();
}
}
