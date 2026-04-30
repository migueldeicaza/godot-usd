#include "usd_mesh_builder.h"

#include <vector>

#include <godot_cpp/classes/box_mesh.hpp>
#include <godot_cpp/classes/capsule_mesh.hpp>
#include <godot_cpp/classes/cylinder_mesh.hpp>
#include <godot_cpp/classes/material.hpp>
#include <godot_cpp/classes/mesh.hpp>
#include <godot_cpp/classes/mesh_instance3d.hpp>
#include <godot_cpp/classes/plane_mesh.hpp>
#include <godot_cpp/classes/sphere_mesh.hpp>
#include <godot_cpp/core/error_macros.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>

#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/token.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usdShade/material.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>

#include "usd_materials.h"
#include "usd_stage_utils.h"

namespace godot_usd {

using namespace godot;
using namespace pxr;

namespace {

struct SurfaceAccumulator {
	PackedVector3Array vertices;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedColorArray colors;
	PackedInt32Array indices;
	Ref<Material> material;
	String usd_material_path;
	String binding_kind;
	String subset_path;
	String subset_name;
};

UsdGeomPrimvar find_uv_primvar(const UsdGeomMesh &p_mesh) {
	UsdGeomPrimvarsAPI primvars_api(p_mesh.GetPrim());
	static const TfToken uv_tokens[] = {
		TfToken("st"),
		TfToken("map1"),
		TfToken("UVMap"),
		TfToken("uvmap"),
	};

	for (const TfToken &uv_token : uv_tokens) {
		UsdGeomPrimvar uv_primvar = primvars_api.FindPrimvarWithInheritance(uv_token);
		if (uv_primvar && uv_primvar.HasValue()) {
			return uv_primvar;
		}
	}

	return UsdGeomPrimvar();
}

UsdGeomPrimvar find_normals_primvar(const UsdGeomMesh &p_mesh) {
	UsdGeomPrimvarsAPI primvars_api(p_mesh.GetPrim());
	UsdGeomPrimvar normals_primvar = primvars_api.FindPrimvarWithInheritance(TfToken("normals"));
	if (normals_primvar && normals_primvar.HasValue()) {
		return normals_primvar;
	}

	return UsdGeomPrimvar();
}

template <typename T>
bool read_interpolated_value(const VtArray<T> &p_values, const TfToken &p_interpolation, int p_face_index, int p_face_vertex_index, int p_point_index, T *r_value) {
	ERR_FAIL_NULL_V(r_value, false);
	if (p_values.empty()) {
		return false;
	}

	int value_index = -1;
	if (p_interpolation == UsdGeomTokens->constant) {
		value_index = 0;
	} else if (p_interpolation == UsdGeomTokens->uniform) {
		value_index = p_face_index;
	} else if (p_interpolation == UsdGeomTokens->vertex || p_interpolation == UsdGeomTokens->varying) {
		value_index = p_point_index;
	} else if (p_interpolation == UsdGeomTokens->faceVarying) {
		value_index = p_face_vertex_index;
	}

	if (value_index < 0 || value_index >= (int)p_values.size()) {
		return false;
	}

	*r_value = p_values[value_index];
	return true;
}

} // namespace

MeshBuildResult build_polygon_mesh(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdGeomMesh &p_mesh, Dictionary *r_mapping_notes) {
	MeshBuildResult result;
	ERR_FAIL_COND_V(p_stage == nullptr, result);

	VtArray<GfVec3f> points;
	VtArray<int> face_vertex_counts;
	VtArray<int> face_vertex_indices;
	if (!p_mesh.GetPointsAttr().Get(&points, p_time) ||
			!p_mesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts, p_time) ||
			!p_mesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices, p_time)) {
		return result;
	}

	TfToken orientation = UsdGeomTokens->rightHanded;
	UsdGeomGprim gprim(p_mesh.GetPrim());
	gprim.GetOrientationAttr().Get(&orientation, p_time);

	UsdGeomPrimvar normals_primvar(p_mesh.GetNormalsAttr());
	if (!normals_primvar || !normals_primvar.HasValue()) {
		normals_primvar = find_normals_primvar(p_mesh);
	}
	VtArray<GfVec3f> normals_values;
	TfToken normals_interpolation = UsdGeomTokens->vertex;
	const bool has_normals = normals_primvar && normals_primvar.ComputeFlattened(&normals_values, p_time);
	if (has_normals) {
		normals_interpolation = normals_primvar.GetInterpolation();
	}

	UsdGeomPrimvar uv_primvar = find_uv_primvar(p_mesh);
	VtArray<GfVec2f> uv_values;
	TfToken uv_interpolation = UsdGeomTokens->faceVarying;
	const bool has_uvs = uv_primvar && uv_primvar.ComputeFlattened(&uv_values, p_time);
	if (has_uvs) {
		uv_interpolation = uv_primvar.GetInterpolation();
	}

	UsdGeomPrimvar display_color_primvar = gprim.GetDisplayColorPrimvar();
	VtArray<GfVec3f> display_colors;
	TfToken display_color_interpolation = UsdGeomTokens->constant;
	const bool has_display_color = display_color_primvar && display_color_primvar.ComputeFlattened(&display_colors, p_time);
	if (has_display_color) {
		display_color_interpolation = display_color_primvar.GetInterpolation();
	}

	std::vector<SurfaceAccumulator> surfaces;
	surfaces.push_back(SurfaceAccumulator());
	surfaces[0].binding_kind = "mesh";

	UsdShadeMaterialBindingAPI mesh_binding_api(p_mesh.GetPrim());
	UsdShadeMaterial default_material = mesh_binding_api.ComputeBoundMaterial();
	if (default_material) {
		surfaces[0].material = build_material_from_usd_material(p_stage, p_time, default_material, r_mapping_notes);
		surfaces[0].usd_material_path = to_godot_string(default_material.GetPath().GetString());
	}

	std::vector<int> face_to_surface(face_vertex_counts.size(), 0);
	const std::vector<UsdGeomSubset> material_subsets = mesh_binding_api.GetMaterialBindSubsets();
	for (const UsdGeomSubset &subset : material_subsets) {
		VtIntArray subset_faces;
		if (!subset.GetIndicesAttr().Get(&subset_faces, p_time)) {
			continue;
		}

		SurfaceAccumulator subset_surface;
		subset_surface.binding_kind = "subset";
		subset_surface.subset_path = to_godot_string(subset.GetPath().GetString());
		subset_surface.subset_name = to_godot_string(subset.GetPrim().GetName().GetString());

		UsdShadeMaterial subset_material = UsdShadeMaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial();
		if (subset_material) {
			subset_surface.material = build_material_from_usd_material(p_stage, p_time, subset_material, r_mapping_notes);
			subset_surface.usd_material_path = to_godot_string(subset_material.GetPath().GetString());
		} else {
			subset_surface.material = surfaces[0].material;
			subset_surface.usd_material_path = surfaces[0].usd_material_path;
		}

		const int surface_index = (int)surfaces.size();
		surfaces.push_back(subset_surface);
		for (int i = 0; i < (int)subset_faces.size(); i++) {
			const int face_index = subset_faces[i];
			if (face_index >= 0 && face_index < (int)face_to_surface.size()) {
				face_to_surface[face_index] = surface_index;
			}
		}
	}

	int face_vertex_cursor = 0;
	for (int face = 0; face < (int)face_vertex_counts.size(); face++) {
		const int count = face_vertex_counts[face];
		if (count < 3 || face_vertex_cursor + count > (int)face_vertex_indices.size()) {
			face_vertex_cursor += count;
			continue;
		}

		SurfaceAccumulator &surface = surfaces[face_to_surface[face]];
		auto append_corner = [&](int p_corner_offset, const Vector3 &p_generated_normal) {
			const int point_index = face_vertex_indices[face_vertex_cursor + p_corner_offset];
			const int face_vertex_index = face_vertex_cursor + p_corner_offset;
			surface.indices.push_back(surface.vertices.size());
			surface.vertices.push_back(Vector3(points[point_index][0], points[point_index][1], points[point_index][2]));

			if (has_normals) {
				GfVec3f normal_value(0.0f);
				if (read_interpolated_value(normals_values, normals_interpolation, face, face_vertex_index, point_index, &normal_value)) {
					surface.normals.push_back(Vector3(normal_value[0], normal_value[1], normal_value[2]));
				} else {
					surface.normals.push_back(p_generated_normal);
				}
			} else {
				surface.normals.push_back(p_generated_normal);
			}

			if (has_uvs) {
				GfVec2f uv_value(0.0f);
				if (read_interpolated_value(uv_values, uv_interpolation, face, face_vertex_index, point_index, &uv_value)) {
					surface.uvs.push_back(Vector2((real_t)uv_value[0], 1.0f - (real_t)uv_value[1]));
				} else {
					surface.uvs.push_back(Vector2());
				}
			}

			if (has_display_color) {
				GfVec3f color_value(1.0f);
				if (read_interpolated_value(display_colors, display_color_interpolation, face, face_vertex_index, point_index, &color_value)) {
					surface.colors.push_back(Color(color_value[0], color_value[1], color_value[2], 1.0f));
				} else {
					surface.colors.push_back(Color(1.0f, 1.0f, 1.0f, 1.0f));
				}
			}
		};

		for (int corner = 1; corner < count - 1; corner++) {
			const int point0_index = face_vertex_indices[face_vertex_cursor + 0];
			const int point1_index = face_vertex_indices[face_vertex_cursor + corner];
			const int point2_index = face_vertex_indices[face_vertex_cursor + corner + 1];
			const Vector3 point0(points[point0_index][0], points[point0_index][1], points[point0_index][2]);
			const Vector3 point1(points[point1_index][0], points[point1_index][1], points[point1_index][2]);
			const Vector3 point2(points[point2_index][0], points[point2_index][1], points[point2_index][2]);

			Vector3 a = point0;
			Vector3 b = point1;
			Vector3 c = point2;
			if (orientation == UsdGeomTokens->rightHanded) {
				b = point2;
				c = point1;
			}

			Vector3 generated_normal = (b - a).cross(c - a);
			if (generated_normal.length_squared() > 0.0f) {
				generated_normal.normalize();
			}

			append_corner(0, generated_normal);
			if (orientation == UsdGeomTokens->rightHanded) {
				append_corner(corner + 1, generated_normal);
				append_corner(corner, generated_normal);
			} else {
				append_corner(corner, generated_normal);
				append_corner(corner + 1, generated_normal);
			}
		}
		face_vertex_cursor += count;
	}

	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	for (int i = 0; i < (int)surfaces.size(); i++) {
		const SurfaceAccumulator &surface = surfaces[i];
		if (surface.vertices.is_empty() || surface.indices.is_empty()) {
			continue;
		}

		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = surface.vertices;
		arrays[Mesh::ARRAY_NORMAL] = surface.normals;
		arrays[Mesh::ARRAY_INDEX] = surface.indices;
		if (!surface.uvs.is_empty()) {
			arrays[Mesh::ARRAY_TEX_UV] = surface.uvs;
		}
		if (!surface.colors.is_empty()) {
			arrays[Mesh::ARRAY_COLOR] = surface.colors;
		}
		mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);

		Ref<Material> surface_material = surface.material;
		if (surface_material.is_null() && has_display_color && display_color_interpolation == UsdGeomTokens->constant && !display_colors.empty()) {
			const GfVec3f color_value = display_colors[0];
			surface_material = make_display_color_material(Color(color_value[0], color_value[1], color_value[2], 1.0f), false);
		} else if (surface_material.is_null() && has_display_color) {
			surface_material = make_display_color_material(Color(1.0f, 1.0f, 1.0f, 1.0f), true);
		}
		if (surface_material.is_valid()) {
			mesh->surface_set_material(mesh->get_surface_count() - 1, surface_material);
		}

		if (!surface.usd_material_path.is_empty()) {
			result.material_paths.push_back(surface.usd_material_path);
		}
		if (!surface.subset_path.is_empty() || !surface.subset_name.is_empty()) {
			Dictionary subset_description;
			subset_description["binding_kind"] = surface.binding_kind;
			if (!surface.subset_path.is_empty()) {
				subset_description["subset_path"] = surface.subset_path;
			}
			if (!surface.subset_name.is_empty()) {
				subset_description["subset_name"] = surface.subset_name;
			}
			if (!surface.usd_material_path.is_empty()) {
				subset_description["material_path"] = surface.usd_material_path;
			}
			result.material_subsets.push_back(subset_description);
		}
	}

	result.mesh = mesh->get_surface_count() > 0 ? mesh : Ref<ArrayMesh>();
	return result;
}

Node *build_primitive_mesh_instance(const UsdTimeCode &p_time, const UsdPrim &p_prim) {
	Ref<Mesh> mesh;

	if (p_prim.IsA<UsdGeomCube>()) {
		Ref<BoxMesh> box_mesh;
		box_mesh.instantiate();
		double size = 2.0;
		UsdGeomCube(p_prim).GetSizeAttr().Get(&size, p_time);
		box_mesh->set_size(Vector3((real_t)size, (real_t)size, (real_t)size));
		mesh = box_mesh;
	} else if (p_prim.IsA<UsdGeomSphere>()) {
		Ref<SphereMesh> sphere_mesh;
		sphere_mesh.instantiate();
		double radius = 1.0;
		UsdGeomSphere(p_prim).GetRadiusAttr().Get(&radius, p_time);
		sphere_mesh->set_radius((real_t)radius);
		mesh = sphere_mesh;
	} else if (p_prim.IsA<UsdGeomCapsule>()) {
		Ref<CapsuleMesh> capsule_mesh;
		capsule_mesh.instantiate();
		double radius = 1.0;
		double height = 2.0;
		UsdGeomCapsule capsule(p_prim);
		capsule.GetRadiusAttr().Get(&radius, p_time);
		capsule.GetHeightAttr().Get(&height, p_time);
		capsule_mesh->set_radius((real_t)radius);
		capsule_mesh->set_height((real_t)height);
		mesh = capsule_mesh;
	} else if (p_prim.IsA<UsdGeomCylinder>()) {
		Ref<CylinderMesh> cylinder_mesh;
		cylinder_mesh.instantiate();
		double radius = 1.0;
		double height = 2.0;
		UsdGeomCylinder cylinder(p_prim);
		cylinder.GetRadiusAttr().Get(&radius, p_time);
		cylinder.GetHeightAttr().Get(&height, p_time);
		cylinder_mesh->set_top_radius((real_t)radius);
		cylinder_mesh->set_bottom_radius((real_t)radius);
		cylinder_mesh->set_height((real_t)height);
		mesh = cylinder_mesh;
	} else if (p_prim.IsA<UsdGeomCone>()) {
		Ref<CylinderMesh> cone_mesh;
		cone_mesh.instantiate();
		double radius = 1.0;
		double height = 2.0;
		UsdGeomCone cone(p_prim);
		cone.GetRadiusAttr().Get(&radius, p_time);
		cone.GetHeightAttr().Get(&height, p_time);
		cone_mesh->set_top_radius(0.0f);
		cone_mesh->set_bottom_radius((real_t)radius);
		cone_mesh->set_height((real_t)height);
		mesh = cone_mesh;
	} else if (p_prim.IsA<UsdGeomPlane>()) {
		Ref<PlaneMesh> plane_mesh;
		plane_mesh.instantiate();
		plane_mesh->set_size(Vector2(2.0f, 2.0f));
		mesh = plane_mesh;
	}

	if (mesh.is_null()) {
		return nullptr;
	}

	MeshInstance3D *mesh_instance = memnew(MeshInstance3D);
	mesh_instance->set_mesh(mesh);
	return mesh_instance;
}

} // namespace godot_usd
