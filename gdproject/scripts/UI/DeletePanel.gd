class_name DeletePanel
extends Panel

## Emitted when the user confirms (was_deleted=true) or cancels (was_deleted=false).
## The actual deletion is the caller's responsibility.
signal delete_finished(path: String, was_deleted: bool)

enum FileKind {
	Map,
	TilePack,
	AssetPack,
}

var _file_kind: FileKind = FileKind.Map
var _json_path: String = ""

@onready var _yes_button: Button = $Panel/HBoxContainer/YesButton
@onready var _no_button: Button = $Panel/HBoxContainer/NoButton
@onready var _delete_text: Label = $Panel/DeleteText


func _ready() -> void:
	visible = false
	process_mode = Node.PROCESS_MODE_DISABLED
	_yes_button.pressed.connect(_on_yes_pressed)
	_no_button.pressed.connect(_on_no_pressed)


func setup(kind: FileKind, path: String) -> void:
	_file_kind = kind
	_json_path = path
	_update_label_text()
	visible = true
	process_mode = Node.PROCESS_MODE_INHERIT


func _update_label_text() -> void:
	var kind_label: String
	if _file_kind == FileKind.Map:
		kind_label = "this map"
	elif _file_kind == FileKind.TilePack:
		kind_label = "this tile pack"
	else:
		kind_label = "this asset pack"
	_delete_text.text = "Confirm deletion of %s?\n%s" % [kind_label, _json_path]


func _on_no_pressed() -> void:
	delete_finished.emit(_json_path, false)
	queue_free()


func _on_yes_pressed() -> void:
	delete_finished.emit(_json_path, true)
	queue_free()
