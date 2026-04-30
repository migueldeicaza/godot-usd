#include "usd_usdz.h"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/zip_reader.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include "usd_stage_utils.h"

namespace godot_usd {

using namespace godot;
using namespace pxr;

String normalize_usd_asset_path(const String &p_asset_path) {
	return p_asset_path.replace("\\", "/");
}

bool split_usdz_package_asset_path(const String &p_asset_path, String *r_package_path, String *r_member_path) {
	ERR_FAIL_NULL_V(r_package_path, false);
	ERR_FAIL_NULL_V(r_member_path, false);

	const int package_delimiter = p_asset_path.rfind("[");
	if (package_delimiter == -1 || !p_asset_path.ends_with("]")) {
		return false;
	}

	*r_package_path = p_asset_path.substr(0, package_delimiter);
	*r_member_path = p_asset_path.substr(package_delimiter + 1, p_asset_path.length() - package_delimiter - 2);
	return !r_package_path->is_empty() && !r_member_path->is_empty();
}

String get_asset_extension(const String &p_asset_path) {
	String extension_path = normalize_usd_asset_path(p_asset_path);
	const int package_delimiter = extension_path.rfind("[");
	if (package_delimiter != -1) {
		extension_path = extension_path.substr(package_delimiter + 1, extension_path.length() - package_delimiter - 1);
	}
	if (extension_path.ends_with("]")) {
		extension_path = extension_path.left(extension_path.length() - 1);
	}
	return extension_path.get_extension().to_lower();
}

bool read_usdz_package_member_bytes(const String &p_package_path, const String &p_member_path, PackedByteArray *r_bytes) {
	ERR_FAIL_NULL_V(r_bytes, false);
	r_bytes->clear();

	Ref<ZIPReader> zip_reader;
	zip_reader.instantiate();
	if (zip_reader->open(p_package_path) != OK) {
		return false;
	}

	const String member_path = p_member_path.replace("\\", "/");
	if (!zip_reader->file_exists(member_path, true) && !zip_reader->file_exists(member_path, false)) {
		zip_reader->close();
		return false;
	}

	*r_bytes = zip_reader->read_file(member_path, zip_reader->file_exists(member_path, true));
	zip_reader->close();
	return !r_bytes->is_empty();
}

Ref<Image> load_image_from_asset_bytes(const PackedByteArray &p_bytes, const String &p_asset_path) {
	if (p_bytes.is_empty()) {
		return Ref<Image>();
	}

	Ref<Image> image;
	image.instantiate();

	const String extension = get_asset_extension(p_asset_path);
	Error err = ERR_FILE_UNRECOGNIZED;
	if (extension == "png") {
		err = image->load_png_from_buffer(p_bytes);
	} else if (extension == "jpg" || extension == "jpeg") {
		err = image->load_jpg_from_buffer(p_bytes);
	} else if (extension == "webp") {
		err = image->load_webp_from_buffer(p_bytes);
	} else if (extension == "tga") {
		err = image->load_tga_from_buffer(p_bytes);
	} else if (extension == "bmp") {
		err = image->load_bmp_from_buffer(p_bytes);
	} else if (extension == "ktx") {
		err = image->load_ktx_from_buffer(p_bytes);
	} else if (extension == "svg") {
		err = image->load_svg_from_buffer(p_bytes);
	}

	if (err != OK) {
		return Ref<Image>();
	}

	return image;
}

String resolve_asset_path(const UsdStageRefPtr &p_stage, const SdfAssetPath &p_asset_path) {
	ERR_FAIL_COND_V(p_stage == nullptr, String());

	const String resolved_path = normalize_usd_asset_path(to_godot_string(p_asset_path.GetResolvedPath()));
	if (!resolved_path.is_empty()) {
		return resolved_path;
	}

	const String authored_path = normalize_usd_asset_path(to_godot_string(p_asset_path.GetAssetPath()));
	if (authored_path.is_empty()) {
		return String();
	}
	if (authored_path.is_absolute_path()) {
		return authored_path;
	}

	String layer_path = normalize_usd_asset_path(to_godot_string(p_stage->GetRootLayer()->GetRealPath()));
	if (layer_path.is_empty()) {
		layer_path = normalize_usd_asset_path(to_godot_string(p_stage->GetRootLayer()->GetIdentifier()));
	}
	if (!layer_path.is_empty()) {
		if (layer_path.contains("[") && layer_path.ends_with("]")) {
			const int package_delimiter = layer_path.rfind("[");
			const String package_path = layer_path.substr(0, package_delimiter);
			const String member_path = layer_path.substr(package_delimiter + 1, layer_path.length() - package_delimiter - 2).get_base_dir();
			return package_path + String("[") + member_path.path_join(authored_path) + String("]");
		}
		return layer_path.get_base_dir().path_join(authored_path);
	}

	return authored_path;
}

Ref<Image> load_image_from_asset_attribute(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdAttribute &p_asset_attribute, String *r_resolved_path) {
	if (!p_asset_attribute) {
		return Ref<Image>();
	}

	SdfAssetPath asset_path;
	if (!p_asset_attribute.Get(&asset_path, p_time)) {
		return Ref<Image>();
	}

	const String resolved_path = resolve_asset_path(p_stage, asset_path);
	if (resolved_path.is_empty()) {
		return Ref<Image>();
	}

	if (r_resolved_path != nullptr) {
		*r_resolved_path = resolved_path;
	}

	String package_path;
	String package_member_path;
	if (split_usdz_package_asset_path(resolved_path, &package_path, &package_member_path)) {
		PackedByteArray asset_bytes;
		if (!read_usdz_package_member_bytes(package_path, package_member_path, &asset_bytes)) {
			return Ref<Image>();
		}
		return load_image_from_asset_bytes(asset_bytes, package_member_path);
	}

	return Image::load_from_file(resolved_path);
}

Ref<Texture2D> texture_from_image(const Ref<Image> &p_image) {
	if (p_image.is_null()) {
		return Ref<Texture2D>();
	}
	return ImageTexture::create_from_image(p_image);
}

Ref<Texture2D> load_texture_from_asset_attribute(const UsdStageRefPtr &p_stage, const UsdTimeCode &p_time, const UsdAttribute &p_asset_attribute, String *r_resolved_path) {
	return texture_from_image(load_image_from_asset_attribute(p_stage, p_time, p_asset_attribute, r_resolved_path));
}

} // namespace godot_usd
