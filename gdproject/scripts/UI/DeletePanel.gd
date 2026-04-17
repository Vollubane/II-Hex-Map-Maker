class_name DeletePanel
extends Panel

signal delete_finished(path: String, was_deleted: bool)

enum FileKind {
	Map,
	TilePack,
}

var _file_kind: FileKind = FileKind.Map
var _json_path: String = ""
var _target_ref: WeakRef

@onready var _yes_button: Button = $Panel/HBoxContainer/YesButton
@onready var _no_button: Button = $Panel/HBoxContainer/NoButton
@onready var _delete_text: Label = $Panel/DeleteText


func _ready() -> void:
	visible = false
	process_mode = Node.PROCESS_MODE_DISABLED
	_yes_button.pressed.connect(_on_yes_pressed)
	_no_button.pressed.connect(_on_no_pressed)


func setup(kind: FileKind, json_path: String, target: Node) -> void:
	_file_kind = kind
	_json_path = json_path
	_target_ref = weakref(target) if target else null
	_update_label_text()
	visible = true
	process_mode = Node.PROCESS_MODE_INHERIT


func _update_label_text() -> void:
	var label := "this map" if _file_kind == FileKind.Map else "this tile pack"
	_delete_text.text = "Confirm deletion of %s?\n\n%s" % [label, _json_path]


func _on_no_pressed() -> void:
	delete_finished.emit(_json_path, false)
	queue_free()


func _on_yes_pressed() -> void:
	var node: Node = _target_ref.get_ref() if _target_ref else null
	if node:
		node.queue_free()
	_remove_json_file()
	delete_finished.emit(_json_path, true)
	queue_free()


func _remove_json_file() -> void:
	if _json_path.is_empty():
		return
	if not FileAccess.file_exists(_json_path):
		push_warning("DeletePanel: file not found: %s" % _json_path)
		return
	var global_path := ProjectSettings.globalize_path(_json_path)
	var err := DirAccess.remove_absolute(global_path)
	if err != OK:
		push_error("DeletePanel: could not delete file (%s): %s" % [str(err), _json_path])
