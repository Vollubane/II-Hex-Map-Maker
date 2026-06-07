class_name GroupDisplayer
extends PanelContainer

## Emitted when the user submits a rename; p_old_name is the previous name, p_new_name the validated one.
signal rename_submitted(p_old_name: String, p_new_name: String)
## Emitted when the delete button is pressed; the parent is responsible for opening the confirmation panel.
signal delete_pressed(p_group_name: String)

const _FORBIDDEN_GROUP_CHARS: Array[String] = ["\\", "\"", ":", "*", "?", "<", ">", "|"]

@onready var _title_label: Label = $MainButton/HBoxContainer/VBoxLabels/TitleSlot/TitleLabel
@onready var _title_line_edit: LineEdit = $MainButton/HBoxContainer/VBoxLabels/TitleSlot/TitleLineEdit
@onready var _delete_button: Button = $MainButton/HBoxContainer/VBoxLabels2/DeleteButton
@onready var _rename_button: Button = $MainButton/HBoxContainer/VBoxLabels2/RenameButton

var _saved_name: String = ""
var _suppress_focus_exit: bool = false


func _ready() -> void:
	set_process_input(false)
	_delete_button.pressed.connect(_on_delete_pressed)
	_rename_button.pressed.connect(_begin_rename)
	_title_line_edit.text_submitted.connect(_on_line_edit_submitted)
	_title_line_edit.focus_exited.connect(_on_focus_exited)


## Sets the displayed group name; also resets the internal saved name.
func set_group_name(p_name: String) -> void:
	_title_label.text = p_name
	_title_line_edit.text = p_name


## Returns the currently displayed group name.
func get_group_name() -> String:
	return _title_label.text


## Applies a color to the group name label so each group is visually distinct.
func set_group_color(p_color: Color) -> void:
	_title_label.add_theme_color_override("font_color", p_color)


func _input(p_event: InputEvent) -> void:
	if not _title_line_edit.visible:
		return
	var mb := p_event as InputEventMouseButton
	if mb and mb.pressed and mb.button_index == MOUSE_BUTTON_LEFT:
		if not _title_line_edit.get_global_rect().has_point(mb.global_position):
			_cancel_rename()
			get_viewport().set_input_as_handled()


func _begin_rename() -> void:
	_saved_name = _title_label.text
	_title_line_edit.text = _saved_name
	_title_label.visible = false
	_title_line_edit.visible = true
	set_process_input(true)
	_title_line_edit.grab_focus()
	_title_line_edit.call_deferred("select_all")


func _cancel_rename() -> void:
	if not _title_line_edit.visible:
		return
	_title_line_edit.text = _saved_name
	_title_label.text = _saved_name
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)


func _on_line_edit_submitted(p_new_name: String) -> void:
	var trimmed := p_new_name.strip_edges()
	if trimmed.is_empty() or trimmed == _saved_name:
		_suppress_focus_exit = true
		_cancel_rename()
		call_deferred("_clear_suppress")
		return
	for ch in _FORBIDDEN_GROUP_CHARS:
		if trimmed.contains(ch):
			SystemEventBus.warning_event.emit("Illegal characters in group name.", 3.5)
			_suppress_focus_exit = true
			_cancel_rename()
			call_deferred("_clear_suppress")
			return
	_suppress_focus_exit = true
	_title_label.text = trimmed
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)
	rename_submitted.emit(_saved_name, trimmed)
	call_deferred("_clear_suppress")


func _on_focus_exited() -> void:
	if _suppress_focus_exit:
		return
	_cancel_rename()


func _clear_suppress() -> void:
	_suppress_focus_exit = false


func _on_delete_pressed() -> void:
	delete_pressed.emit(_title_label.text)
