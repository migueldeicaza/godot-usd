# godot-usd

Standalone `godot-cpp` GDExtension port scaffold for the USD module in `modules/usd`.

Current scope:

- registers `UsdStageResource` and `UsdStageInstance`
- registers a runtime `ResourceFormatLoader` for `.usd`, `.usda`, `.usdc`, `.usdz`
- registers a stub `ResourceFormatSaver`
- registers an editor `EditorSceneFormatImporter` through an `EditorPlugin`
- ports a first-pass USD scene builder:
  - hierarchy
  - transforms
  - stage metadata
  - variant catalog and variant selection
  - polygon meshes
  - authored normals and UV primvars
  - displayColor fallback materials
  - `UsdPreviewSurface` materials
  - mesh material subsets
  - packaged USDZ texture assets for supported image formats
  - primitive meshes
  - cameras
  - distant and sphere lights

Build:

```bash
cd godot-usd
scons platform=macos target=template_debug arch=arm64
scons platform=macos target=template_release arch=arm64
```

Useful overrides:

```bash
scons godot_cpp_path=/path/to/godot-cpp usd_sdk_path=/path/to/usd/install
```

Open `godot-usd/project` in Godot after building. The extension manifest is `project/godot_usd.gdextension`.

Smoke test:

```bash
/path/to/godot --headless --path project --script res://test_load.gd
```

Known gaps:

- USD save/export is stubbed
- the editor importer currently bakes through `UsdStageInstance`, not a direct port of the in-tree importer
- skeletons, blend shapes, animation, and USDZ save/repack support are not ported yet
