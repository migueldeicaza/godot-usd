extends SceneTree

func _initialize() -> void:
    call_deferred("_run_test")

func _run_test() -> void:
    var path := "res://samples/cube.usda"
    var args := OS.get_cmdline_user_args()
    if not args.is_empty():
        path = args[0]

    var packed = load(path)
    if packed == null:
        push_error("Failed to load USD scene: %s" % path)
        quit(1)
        return

    if not (packed is PackedScene):
        push_error("Loaded resource is not a PackedScene: %s" % [packed])
        quit(2)
        return

    var root = packed.instantiate()
    if root == null:
        push_error("Failed to instantiate USD PackedScene")
        quit(3)
        return

    get_root().add_child(root)

    print("USD root: %s (%s)" % [root.name, root.get_class()])
    print("USD children: %d" % root.get_child_count())
    var generated: Node = null
    var mesh_instance: MeshInstance3D = null
    var material: Material = null
    if root.get_child_count() > 0:
        generated = root.get_child(0)
        print("Generated root: %s (%s), children=%d" % [generated.name, generated.get_class(), generated.get_child_count()])
        mesh_instance = _find_first_mesh_instance(generated)
        if mesh_instance != null and mesh_instance.mesh != null and mesh_instance.mesh.get_surface_count() > 0:
            material = mesh_instance.mesh.surface_get_material(0)
            if material != null:
                print("Surface material: %s" % material.get_class())
                if material is BaseMaterial3D:
                    var base_material := material as BaseMaterial3D
                    print("Has albedo texture: %s" % [base_material.get_texture(BaseMaterial3D.TEXTURE_ALBEDO) != null])
                    print("Has clearcoat texture: %s" % [base_material.get_texture(BaseMaterial3D.TEXTURE_CLEARCOAT) != null])
                    print("Has AO texture: %s" % [base_material.get_texture(BaseMaterial3D.TEXTURE_AMBIENT_OCCLUSION) != null])
                    print("UV1 scale: %s" % [base_material.uv1_scale])
                    print("UV1 offset: %s" % [base_material.uv1_offset])
        if mesh_instance != null and mesh_instance.has_meta("usd"):
            print("Mesh USD meta: %s" % [mesh_instance.get_meta("usd")])

    material = null
    mesh_instance = null
    generated = null
    packed = null

    var cleanup_root: Node = root
    root = null
    cleanup_root.free()
    cleanup_root = null

    await process_frame
    quit(0)

func _find_first_mesh_instance(node: Node) -> MeshInstance3D:
    if node is MeshInstance3D:
        return node
    for child in node.get_children():
        var found := _find_first_mesh_instance(child)
        if found != null:
            return found
    return null
