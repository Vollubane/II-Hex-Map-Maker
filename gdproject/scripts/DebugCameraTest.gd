extends Node

func _ready() -> void:
	randomize()
	for child in get_children():
		if child is MeshInstance3D:
			var material := StandardMaterial3D.new()
			material.albedo_color = Color(randf(), randf(), randf())
			child.material_override = material
