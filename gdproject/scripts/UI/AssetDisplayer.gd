class_name AssetDisplayer
extends PanelContainer

## Emitted when the toggle enters the pressed state (selected).
signal selected
## Emitted when the toggle returns to unpressed state (deselected).
signal deselected
## Fired after a rename is committed (Enter), or aborted via click-out cancel flow.
signal title_submitted(new_title: String)

@onready var _main_button: Button = $MainButton
@onready var _texture_rect: TextureRect = $MainButton/VBoxContainer/Panel/TextureRect
@onready var _title_label: Label = $MainButton/VBoxContainer/HBoxContainer/VBoxLabels/TitleSlot/TitleLabel
@onready var _title_line_edit: LineEdit = $MainButton/VBoxContainer/HBoxContainer/VBoxLabels/TitleSlot/TitleLineEdit
@onready var _rename_button: Button = $MainButton/VBoxContainer/HBoxContainer/RenameButton

var _saved_title: String = ""
var _suppress_focus_exit: bool = false
## True only between rename open and submit/cancel; avoids stray focus_exit clearing the label on main toggle click.
var _rename_session_active: bool = false

const _FORBIDDEN_FILENAME_CHARS: Array[String] = ["/", "\\", "\"", ":", "*", "?", "<", ">", "|"]


func _title_contains_forbidden_filename_chars(s: String) -> bool:
	for ch in _FORBIDDEN_FILENAME_CHARS:
		if s.contains(ch):
			return true
	return false


func _ready() -> void:
	_title_label.visible = true
	_title_line_edit.visible = false
	_rename_session_active = false
	set_process_input(false)
	_main_button.toggled.connect(_on_main_button_toggled)
	_rename_button.pressed.connect(_begin_title_edit)
	_title_line_edit.text_submitted.connect(_on_title_line_edit_submitted)
	_title_line_edit.focus_exited.connect(_on_title_line_edit_focus_exited)


func _input(event: InputEvent) -> void:
	if not _rename_session_active or not _title_line_edit.visible:
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


func _on_main_button_toggled(button_pressed: bool) -> void:
	if button_pressed:
		selected.emit()
	else:
		deselected.emit()


## Updates preview texture and label / LineEdit backing text without touching toggle state (`null` texture clears preview).
func set_preview_and_title(tex: Texture2D, title: String) -> void:
	_texture_rect.texture = tex
	set_title(title)


func set_title(text: String) -> void:
	_title_label.text = text
	_title_line_edit.text = text


func set_button_pressed(on: bool) -> void:
	_main_button.set_pressed_no_signal(on)


func unpress_button() -> void:
	if not _main_button.button_pressed:
		return
	_main_button.set_pressed_no_signal(false)


func is_button_pressed() -> bool:
	return _main_button.button_pressed


## Overrides the normal-state border color to reflect the group this asset belongs to.
func set_group_color(p_color: Color) -> void:
	var style := StyleBoxFlat.new()
	style.bg_color = Color(0.188235, 0.215686, 0.262745, 1)
	style.set_border_width_all(4)
	style.border_color = p_color
	style.set_corner_radius_all(12)
	_main_button.add_theme_stylebox_override("normal", style)


## Removes the group color override and restores the default border.
func clear_group_color() -> void:
	_main_button.remove_theme_stylebox_override("normal")


func _begin_title_edit() -> void:
	_rename_session_active = true
	_saved_title = _title_label.text
	_title_line_edit.text = _saved_title
	_title_label.visible = false
	_title_line_edit.visible = true
	set_process_input(true)
	_title_line_edit.grab_focus()
	_title_line_edit.call_deferred("select_all")


func _cancel_title_edit() -> void:
	if not _rename_session_active:
		return
	_rename_session_active = false
	var restore := _saved_title
	if restore.is_empty():
		restore = _title_label.text
	if restore.is_empty():
		restore = _title_line_edit.text
	_title_line_edit.text = restore
	_title_label.text = restore
	_title_line_edit.visible = false
	_title_label.visible = true
	set_process_input(false)


func _on_title_line_edit_submitted(new_text: String) -> void:
	if _title_contains_forbidden_filename_chars(new_text):
		SystemEventBus.warning_event.emit("Illegal characters in name.", 3.5)
		_suppress_focus_exit = true
		_cancel_title_edit()
		call_deferred("_clear_suppress_focus_deferred")
		return
	_suppress_focus_exit = true
	_rename_session_active = false
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
	if not _rename_session_active:
		return
	if not _title_line_edit.visible:
		return
	_cancel_title_edit()
