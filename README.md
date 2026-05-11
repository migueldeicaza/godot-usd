# godot-usd

A GDExtension that brings [Pixar's OpenUSD](https://openusd.org/) into Godot. It lets a Godot
project read `.usd`, `.usda`, `.usdc`, and `.usdz` files and use them either as live USD stages
inside a scene or as a one-shot import that bakes a USD asset into a regular Godot scene.

https://github.com/user-attachments/assets/1a2621ea-42ac-4c43-bd41-304dec1ab618

## What This Module Does

The extension plugs three things into the engine:

- **A runtime resource loader** for the USD file extensions. Any `load()` call on a USD path
  returns a `UsdStageResource` (the parsed stage) plus an instantiable `UsdStageInstance` node
  that builds the corresponding Godot scene tree.
- **A runtime resource saver** that can write Godot-side edits back to USD, including a
  source-aware path that copies and patches the original USD layer instead of regenerating it
  from scratch.
- **An editor scene importer** (`EditorSceneFormatImporter`) that bakes a USD file into a Godot
  `PackedScene` (`.tscn`/`.scn`) the way other importers do for glTF or FBX.

Once installed, USD files appear in the Godot editor's filesystem dock like any other scene
asset. Drag one into a scene, instantiate it from code, or let the importer turn it into a
native Godot scene.

## Supported USD Features

Geometry and topology
- Polygon meshes, including authored normals and UV primvars (vertex, face-varying, and
  uniform interpolation)
- Right-handed and left-handed winding preservation
- Curves: linear and Bezier basis curves
- Primitive meshes (cube/sphere/plane/cylinder schemas)

Materials and textures
- `UsdPreviewSurface` with scalar/vector inputs (diffuse, metallic, roughness, opacity,
  normal, emissive, clearcoat, etc.)
- Texture networks via `UsdUVTexture` with shared `UsdTransform2d` UV transforms and channel
  mapping
- Texture assets packaged inside `.usdz` archives, decoded for supported image formats
- `displayColor` fallback materials when no shader graph is authored
- Material subsets, including named, generic, edge, point, inherited, and unmapped subsets

Lighting and cameras
- Distant, sphere, cylinder, rectangular, and disk light schemas; rectangular and disk lights
  fall back to a `UsdAreaLightProxy` node so authored data survives on runtimes without
  `AreaLight3D`
- Emissive material/light combinations
- `UsdGeomCamera`

Rigging and animation
- `UsdSkelSkeleton`, with rest transforms, joint hierarchies, and direct skeleton bindings
- Skinning weights (up to 8 influences per vertex), including point-based bindings
- Blend shapes, including animation tracks
- Sample-domain animation metadata and stage-correction edge cases

Stage and composition
- Stage metadata (up-axis, meters-per-unit, time codes, etc.)
- Variant sets and variant selection, including a catalog exposed both to runtime instances
  and to the editor importer
- Read-only composition arcs: references, payloads, inherits, specializes

## Installing in a Godot Project

1. Build the extension binary (see the *Building from source* section below).
2. Copy the built library plus `project/godot_usd.gdextension` into your project. The
   manifest expects the binaries at `res://bin/libgodot_usd.*`.
3. Open the project in Godot. The extension registers itself on first launch and `.usd*`
   files start appearing as scene assets in the filesystem dock.

The extension targets `compatibility_minimum = "4.3"`. The local validated stack is Godot
4.7.dev (`181b24ba2`) with `godot-cpp` at `6388e26`, OpenUSD 24.11
(`PXR_VERSION 2608`), and TBB 2022.3.0.
Currently shipped binaries target `macos.arm64`; other platforms need a local build.

## Two Ways to Use a USD Asset

The same USD file can be consumed in two distinct ways. They are not mutually exclusive — pick
per asset based on whether you want the USD-side authoring graph to remain live at runtime.

### 1. Live USD stage (use the USD scene as-is)

This is the "USD stays USD" path. The file is loaded as a `UsdStageResource`, instanced as a
`UsdStageInstance` node, and the Godot subtree is generated from the composed stage at
runtime. The stage resource, variant selections, and composition arcs stay attached to the
node, which means:

- You can change variant selections at runtime (`set_variant_selections({...})` followed by
  `rebuild()`) and the subtree is rebuilt against the same composed stage.
- Authored composition (references, payloads, inherits, specializes) is preserved on the
  resource side, so saves can round-trip through the source layer.
- The same USD file shared between scenes still benefits from cached parsing — `load()`
  returns the same `UsdStageResource`.

Typical use: drop a `UsdStageInstance` into your scene, point its `stage` property at the
`.usd*` file, and pick variant selections in the inspector. The generated tree appears under a
`_Generated` child node so your scene file stays small.

From code:

```gdscript
var stage := load("res://props/lamp.usda") as UsdStageResource
var instance := UsdStageInstance.new()
instance.stage = stage
instance.variant_selections = {"/Lamp": {"shade": "frosted"}}
add_child(instance)
instance.rebuild()
```

### 2. One-off (re)import (use the USD file as an importer)

This is the "USD becomes a Godot scene" path. The editor's scene format importer reads the
USD file and bakes it into a native Godot `PackedScene`. The resulting scene contains plain
`Node3D`/`MeshInstance3D`/`Skeleton3D`/etc. nodes — no `UsdStageInstance` is left at runtime.

- Variant choices made in the import dock are baked at import time. The importer exposes a
  per-variant-set dropdown built from the stage's variant catalog. The chosen selections are
  also recorded in node metadata for traceability.
- Re-importing replays the bake with whatever options you change, so you can swap variants or
  tweak settings later without touching the original USD file.
- The importer emits a warning when it bakes a stage that has variants, reminding you that
  inactive branches and live variant switching are not preserved in the imported result.

Use this when the asset is content you would otherwise have shipped as a `.glb` — a finished
prop, a character, an environment chunk — and you do not need the USD authoring graph at
runtime.

## Writing Content Back to USD

The extension registers a `ResourceFormatSaver` for the same USD extensions, so
`ResourceSaver.save()` on a USD-derived resource produces a USD file. The saver has several
distinct save paths and picks the most conservative one that fits the resource:

- **Authored-scene save.** Writes a newly authored scene (typically the result of building a
  scene from Godot code) to a USDA layer with the structural pieces the loader supports:
  hierarchy, transforms, primitive meshes, ArrayMesh data, preview materials, texture
  networks, material subsets, lights, skeletons, and blend-shape/animation tracks.
- **Composed-stage save.** Writes the composed scene corresponding to a `UsdStageInstance`,
  including the variant selections applied at save time.
- **Source-preserving save.** When a static USD scene was imported (no live composition
  edits), the saver copies the source layer and authors only the supported edits back into
  the copy. The original prim structure, layer arcs, and any authoring not touched by Godot
  are preserved verbatim. The currently supported edit categories are:
  - Local `Node3D` transform edits
  - Scalar/color `UsdPreviewSurface` material input edits (diffuse, metallic, roughness,
    opacity)
  - Same-topology `UsdGeomMesh.points` edits, when Godot vertices map back to USD point
    indices
  - `UsdSkelSkeleton` rest transform edits
  - `UsdSkelAnimation` translation, rotation, scale, and direct primary blend-shape weight
    key edits
- **Variant-default `.usdz` save.** Composition arcs and the variant default selection are
  preserved when re-saving a packaged USDZ.

Edits the saver cannot safely merge — mesh topology changes, mesh index/primvar rewrites,
whole-material replacement or rebinding, material subset rebinding, texture-network material
edits, and derived inbetween blend-shape tracks — produce an explicit warning. In those
cases the source data is left untouched rather than silently rewritten in a way that could
corrupt downstream USD pipelines.

## Building from Source

This is a GDExtension build, so SCons needs access to three external SDKs:

- **godot-cpp**: a checkout of the Godot C++ bindings that matches the Godot version you are
  targeting. Set `GODOT_CPP_PATH` or pass `godot_cpp_path=/path/to/godot-cpp`.
- **OpenUSD**: an installed OpenUSD SDK prefix containing `include/` and `lib/`. Set
  `USD_SDK_PATH` or pass `usd_sdk_path=/path/to/usd/install`. The build links against the USD
  core, geometry, shade, skeleton, lux, and USDZ utility libraries.
- **TBB**: Intel/oneTBB headers and libraries used by OpenUSD. The build links `libtbb`, so
  the TBB library must be discoverable by the linker. Set `TBB_SDK_PATH` or pass
  `tbb_sdk_path=/path/to/tbb`. On Apple Silicon with Homebrew this is commonly
  `/opt/homebrew/opt/tbb`.

The local validated stack is Godot 4.7.dev (`181b24ba2`), `godot-cpp` `6388e26`, and
OpenUSD 24.11 (`PXR_VERSION 2608`) with TBB 2022.3.0.

```bash
cd godot-usd
scons platform=macos target=template_debug arch=arm64
scons platform=macos target=template_release arch=arm64
```

You can provide dependency paths either as environment variables:

```bash
export GODOT_CPP_PATH=/path/to/godot-cpp
export USD_SDK_PATH=/path/to/usd/install
export TBB_SDK_PATH=/path/to/tbb
scons platform=macos target=template_debug arch=arm64
```

Or as SCons overrides:

```bash
scons platform=macos target=template_debug arch=arm64 \
  godot_cpp_path=/path/to/godot-cpp \
  usd_sdk_path=/path/to/usd/install \
  tbb_sdk_path=/path/to/tbb
```

Open `godot-usd/project` in Godot after building. The extension manifest is
`project/godot_usd.gdextension`.

Smoke test:

```bash
/path/to/godot --headless --path project --script res://test_load.gd
```

Regression tests (53 isolated scripts covering the supported feature surface):

```bash
/path/to/godot --headless --path project --script res://tests/run_all.gd
```

## Fixture Provenance

- Most `.usda` files under `project/samples/` are small hand-authored parity fixtures for the
  GDExtension test harness.
- `project/samples/packaged_preview.usdz` is copied from the module test fixture at
  `../tests/data/usd/packaged_preview.usdz`.
- `project/samples/preview_surface_packaged.usdz` is generated from
  `project/samples/package_src/preview_surface_packaged.usda` and
  `project/samples/package_src/textures/albedo.svg`.
- `project/samples/vehicleVariants.selfcontained.usdz` is copied from the local USD sample
  asset at `../thirdparty/vehicleVariants.selfcontained.usdz`.
- Texture fixtures that reference `../images/icon.png` use the repository image at
  `project/images/icon.png`.
