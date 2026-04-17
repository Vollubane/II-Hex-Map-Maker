class_name DataDisplayer
extends PanelContainer

signal delete_pressed
signal launch_pressed
signal empty_pressed
signal title_submitted(new_title: String)

@onready var _title_label: Label = $HBoxContainer/LabelContainer/TitleSlot/TitleLabel
@onready var _title_line_edit: LineEdit = $HBoxContainer/LabelContainer/TitleSlot/TitleLineEdit
@onready var _info_label: Label = $HBoxContainer/LabelContainer/InfoLabel
@onready var _delete_button: Button = $HBoxContainer/ButtonGrid/DeleteButton
@onready var _launch_button: Button = $HBoxContainer/ButtonGrid/LaunchButton
@onready var _rename_button: Button = $HBoxContainer/ButtonGrid/RenameButton
@onready var _empty_button: Button = $HBoxContainer/ButtonGrid/EmptyButton

var _saved_title: String = ""
var _suppress_focus_exit: bool = false

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]


func _title_contains_forbidden_filename_chars(s: String) -> bool:
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(ch):
			return true
	return false


func _ready() -> void:
	_delete_button.pressed.connect(_on_delete_pressed)
	_launch_button.pressed.connect(_on_launch_pressed)
	_rename_button.pressed.connect(_begin_title_edit)
	_empty_button.pressed.connect(_on_empty_pressed)
	_title_line_edit.text_submitted.connect(_on_title_line_edit_submitted)
	_title_line_edit.focus_exited.connect(_on_title_line_edit_focus_exited)


func _input(event: InputEvent) -> void:
	if not _title_line_edit.visible:
		return
	var global_pt: Vector2
	if event is InputEventMouseButton:
		var mb := event as InputEventMouseButton
		if not mb.pressed or mb.button_index != MOUSE_BUTTON_LEFT:
			return
		global_pt = mb.global_position
	elif event is InputEventScreenTouch:
		var st := event as InputEventScreenTouch
		if not st.pressed:
			return
		global_pt = st.position
	else:
		return
	if _title_line_edit.get_global_rect().has_point(global_pt):
		return
	_cancel_title_edit()
	get_viewport().set_input_as_handled()


func get_panel_parent() -> Node:
	return get_parent()


func set_title(text: String) -> void:
	_title_label.text = text
	_title_line_edit.text = text


func set_info(text: String) -> void:
	_info_label.text = text


func set_title_and_info(title: String, info: String) -> void:
	set_title(title)
	_info_label.text = info


func _begin_title_edit() -> void:
	_saved_title = _title_label.text
	_title_line_edit.text = _saved_title
	_title_label.visible = false
	_title_line_edit.visible = true
	set_process_input(true)
	_title_line_edit.grab_focus()
	_title_line_edit.call_deferred("select_all")


func _cancel_title_edit() -> void:
	if not _title_line_edit.visible:
		return
	_title_line_edit.text = _saved_title
	_title_label.text = _saved_title
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)


func _on_title_line_edit_submitted(new_text: String) -> void:
	if _title_contains_forbidden_filename_chars(new_text):
		_suppress_focus_exit = true
		_cancel_title_edit()
		call_deferred("_clear_suppress_focus_deferred")
		return
	_suppress_focus_exit = true
	_title_label.text = new_text
	_title_line_edit.text = new_text
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)
	title_submitted.emit(new_text)
	call_deferred("_clear_suppress_focus_deferred")


func _clear_suppress_focus_deferred() -> void:
	_suppress_focus_exit = false


func _on_title_line_edit_focus_exited() -> void:
	if _suppress_focus_exit:
		return
	if not _title_line_edit.visible:
		return
	_cancel_title_edit()


func _on_delete_pressed() -> void:
	delete_pressed.emit()


func _on_launch_pressed() -> void:
	launch_pressed.emit()


func _on_empty_pressed() -> void:
	empty_pressed.emit()
