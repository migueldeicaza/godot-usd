#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <pxr/usd/sdf/assetPath.h>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usd/timeCode.h>

namespace godot_usd {

using namespace godot;
using namespace pxr;

String normalize_usd_asset_path(const String &p_asset_path);
bool split_usdz_package_asset_path(const String &p_asset_path, String *r_package_path, String *r_member_path);
String get_asset_extension(const String &p_asset_path);
bool read_usdz_package_member_bytes(const String &p_package_path, const String &p_member_path, PackedByteArray *r_bytes);
Ref<Image> load_image_from_asset_bytes(const PackedByteArray &p_bytes, const String &p_asset_path);
String resolve_asset_path(const UsdStageRefPtr &p_stage, const SdfAssetPath &p_asset_path);
Ref<Image> load_image_from_asset_attribute(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdAttribute &p_asset_attribute, String *r_resolved_path = nullptr);
Ref<Texture2D> texture_from_image(const Ref<Image> &p_image);
Ref<Texture2D> load_texture_from_asset_attribute(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdAttribute &p_asset_attribute, String *r_resolved_path = nullptr);

} // namespace godot_usd
