# godot-usd

Standalone `godot-cpp` GDExtension port scaffold for the USD module in `modules/usd`.

Current scope:

- registers `UsdStageResource` and `UsdStageInstance`
- registers a runtime `ResourceFormatLoader` for `.usd`, `.usda`, `.usdc`, `.usdz`
- registers a `ResourceFormatSaver` with authored-scene, composed-stage, static-source-preserving, static transform/material/mesh-point/skeleton/animation edit merge, composition-preserving, and source-preserving variant save paths
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
  - distant, sphere, cylinder, rectangular, and disk light schemas
  - curves, skeletons, skinning, blend shapes, and animation import coverage used by the current probes
  - baseline USDA save/load for authored scenes, primitive meshes, ArrayMesh, preview materials, texture networks, subsets, and variant-preserving source saves

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

Regression tests:

```bash
/path/to/godot --headless --path project --script res://tests/run_all.gd
```

The current regression manifest runs 46 isolated scripts and passes locally with the debug macOS arm64 build.

Known gaps:

- save/export is not yet full module parity for every edit-aware scene case
- broader runtime mesh/material edge cases still need audit against `modules/usd/tests`
- packaging docs still need exact supported Godot, `godot-cpp`, OpenUSD, and TBB version notes
