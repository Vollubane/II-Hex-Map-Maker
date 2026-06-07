class_name DeleteGroupPanel
extends Panel

## Emitted when the user confirms deletion.
## p_delete_assets = true  → delete the group AND all its assets from the manifest.
## p_delete_assets = false → delete the group only; its assets become ungrouped.
signal deletion_confirmed(p_delete_assets: bool)

@onready var _delete_all_button: Button = $Panel/HBoxContainer/DeleteAll
@onready var _delete_group_button: Button = $Panel/HBoxContainer/DeleteGroup
@onready var _cancel_button: Button = $Panel/HBoxContainer/Cancel
@onready var _delete_text: Label = $Panel/DeleteText


func _ready() -> void:
	_delete_all_button.pressed.connect(func(): _confirm(true))
	_delete_group_button.pressed.connect(func(): _confirm(false))
	_cancel_button.pressed.connect(queue_free)


## Fills the description label with the group name being targeted.
func setup(p_group_name: String) -> void:
	_delete_text.text = "Delete group  \"%s\"  ?" % p_group_name


func _confirm(p_delete_assets: bool) -> void:
	deletion_confirmed.emit(p_delete_assets)
	queue_free()
