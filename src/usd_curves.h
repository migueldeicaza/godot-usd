#pragma once

#include <godot_cpp/classes/node3d.hpp>
#include <godot_cpp/variant/dictionary.hpp>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/timeCode.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

Node3D *build_basis_curves_node(const UsdPrim &p_prim, const UsdTimeCode &p_time, Dictionary *r_mapping_notes);

} // namespace godot_usd
