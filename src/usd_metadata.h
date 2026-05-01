#pragma once

#include <godot_cpp/classes/object.hpp>

#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/timeCode.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

void store_unmapped_properties(const UsdPrim &p_prim, const UsdTimeCode &p_time, Object *p_target);
void store_composition_arcs(const UsdPrim &p_prim, Object *p_target);

} // namespace godot_usd
