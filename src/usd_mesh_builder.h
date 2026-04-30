#pragma once

#include <godot_cpp/classes/array_mesh.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>
#include <pxr/usd/usdGeom/mesh.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

struct MeshBuildResult {
	Ref<ArrayMesh> mesh;
	Array material_paths;
	Array material_subsets;
	bool has_skinning = false;
};

MeshBuildResult build_polygon_mesh(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdGeomMesh &p_mesh, Dictionary *r_mapping_notes = nullptr);
Node *build_points_instance(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdPrim &p_prim, Dictionary *r_mapping_notes = nullptr);
Node *build_primitive_mesh_instance(const UsdTimeCode &p_time, const UsdPrim &p_prim);

} // namespace godot_usd
