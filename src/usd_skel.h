#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/classes/skeleton3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/transform3d.hpp>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

Skeleton3D *build_skeleton_node(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const Transform3D &p_stage_correction_transform, const UsdPrim &p_prim, Dictionary *r_mapping_notes);
void append_skin_bindings(Node3D *p_scene_root);
void append_baked_skeleton_animations(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, Node3D *p_scene_root);

} // namespace godot_usd
